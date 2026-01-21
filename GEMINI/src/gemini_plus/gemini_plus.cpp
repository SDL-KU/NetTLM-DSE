#include "gemini_plus/gemini_plus.h"

gemini_plus::gemini_plus()
{
    // std::cout << "gemini_plus::gemini_plus()" << std::endl;
    op_names.push_back("WordEmbedding");
    op_names.push_back("FC-Q");
    op_names.push_back("FC-V");
    for (int k = 0; k < 8; k++)
    {
        op_names.push_back("FC-K" + std::to_string(k));
        op_names.push_back("Transpose-K" + std::to_string(k));
    }
    op_names.push_back("Merge-K");

    // Layer Type
    op_type[0] = OpType::ELEMENT_WISE;
    op_type[1] = OpType::GEMM;
    op_type[2] = OpType::GEMM;
    for (int k = 0; k < 8; k++)
    {
        op_type[3 + k * 2] = OpType::GEMM;
        op_type[4 + k * 2] = OpType::TRANSPOSE;
    }
    op_type[19] = OpType::ELEMENT_WISE;

    // Pipeline Depth
    pipe_depth[0] = 0;
    pipe_depth[1] = 1;
    pipe_depth[2] = 1;
    for (int k = 0; k < 8; k++)
    {
        pipe_depth[3 + k * 2] = 1;
        pipe_depth[4 + k * 2] = 2;
    }
    pipe_depth[19] = 3;

    //
    ifm_dim[0] = {1, 512, 1, 512};
    ifm_dim[1] = {1, 512, 1, 512};
    ifm_dim[2] = {1, 512, 1, 512};
    for (int k = 0; k < 8; k++)
    {
        ifm_dim[3 + k * 2] = {1, 512, 1, 512};
        ifm_dim[4 + k * 2] = {1, 512, 1, 64};
    }
    ifm_dim[19] = {1, 512, 1, 512};

    //
    wgt_dim[0] = {0, 0, 0, 0};
    wgt_dim[1] = {1, 512, 1, 512};
    wgt_dim[2] = {1, 512, 1, 512};
    for (int k = 0; k < 8; k++)
    {
        wgt_dim[3 + k * 2] = {1, 512, 1, 64};
        wgt_dim[4 + k * 2] = {0, 0, 0, 0};
    }
    wgt_dim[19] = {1, 512, 1, 512};

    //
    ofm_dim[0] = {1, 512, 1, 512};
    ofm_dim[1] = {1, 512, 1, 512};
    ofm_dim[2] = {1, 512, 1, 512};
    for (int k = 0; k < 8; k++)
    {
        ofm_dim[3 + k * 2] = {1, 512, 1, 64};
        ofm_dim[4 + k * 2] = {1, 64, 1, 512};
    }
    ofm_dim[19] = {1, 512, 1, 512};

    //
    input_op[0] = {""};
    input_op[1] = {"WordEmbedding"};
    input_op[2] = {"WordEmbedding"};
    for (int k = 0; k < 8; k++)
    {
        input_op[3 + k * 2] = {"WordEmbedding"};
        input_op[4 + k * 2] = {"FC-K" + std::to_string(k)};
        input_op[19].push_back("Transpose-K" + std::to_string(k));
    }

    //
    part[0] = {3, 1};
    part[1] = {8, 1};
    part[2] = {8, 1};
    for (int i = 3; i < 20; i++)
        part[i] = {1, 1};
}

void gemini_plus::init()
{
    // std::cout << "gemini_plus::init()" << std::endl;
    df = new DataFlow();
    allDRAMs.clear();
    allDRAMs.resize(4);
    for (int d = 0; d < 4; d++)
    {
        allDRAMs[d].dramID = d;
        allDRAMs[d].type = "general";
    }
    //
    for (int d = 0; d < 20; d++)
    {
        if ((data_dram[d][1] != -1) && (op_type[d] == OpType::GEMM) || (op_type[d] == OpType::ATTENTION))
            has_Wgt.set(d);
    }

    for (int l = 0; l < 20; l++)
    {
        Operation oper;
        oper.name = op_names[l];
        oper.type = op_type[l];
        oper.hasWeight = has_Wgt[l];
        oper.pipe_depth = pipe_depth[l];
        oper.ifmDim = ifm_dim[l];
        oper.wgtDim = wgt_dim[l];
        oper.ofmDim = ofm_dim[l];
        oper.ifmDRAM = data_dram[l][0];
        if (oper.hasWeight)
            oper.wgtDRAM = data_dram[l][1];
        else
            oper.wgtDRAM = -1;
        oper.ofmDRAM = data_dram[l][2];

        int part_row = part[l].first;
        int part_col = part[l].second;
        int nPart = 0;
        int startRow = 0, endRow, startCol = 0, endCol;

        int remainRows = oper.ofmDim.rows;
        int remainCols = oper.ofmDim.cols;

        if (part_row > 1)
            nPart = part_row;
        else
            nPart = part_col;

        int rowSize = 0, colSize = 0;
        for (int i = 0; i < nPart; i++)
        {
            if (part_row > 1)
            {
                rowSize = ceil((double)remainRows / (nPart - i));
                remainRows -= rowSize;
                endRow = startRow + rowSize - 1;

                startCol = 0;
                endCol = oper.ofmDim.cols - 1;
            }
            else
            {
                colSize = ceil((double)remainCols / (nPart - i));
                remainCols -= colSize;
                endCol = startCol + colSize - 1;

                startRow = 0;
                endRow = oper.ofmDim.rows - 1;
            }
            oper.ofmPartitions.push_back({assignedDpath[l][i], 1, startRow, endRow, 1, startCol, endCol});

            if (part_row > 1)
                startRow += rowSize;
            else
                startCol += colSize;
        }
        df->addOp(oper);

        if (oper.ifmDRAM > -1)
        {
            // LOG_T(oper.name << " IFM DRAM: " << oper.ifmDRAM);
            allDRAMs[oper.ifmDRAM].segments.push_back({"input", oper.name, {oper.ifmDRAM, oper.ifmDim.n_r, 0, oper.ifmDim.rows - 1, oper.ifmDim.n_c, 0, oper.ifmDim.cols - 1}});
        }

        if (oper.wgtDRAM > -1)
        {
            // LOG_T(oper.name << " WGT DRAM: " << oper.wgtDRAM);
            allDRAMs[oper.wgtDRAM].segments.push_back({"weight", oper.name, {oper.wgtDRAM, oper.wgtDim.n_r, 0, oper.wgtDim.rows - 1, oper.wgtDim.n_c, 0, oper.wgtDim.cols - 1}});
        }

        if (oper.ofmDRAM > -1)
        {
            // LOG_T(oper.name << " OFM DRAM: " << oper.ofmDRAM);
            allDRAMs[oper.ofmDRAM].segments.push_back({"output", oper.name, {oper.ofmDRAM, oper.ofmDim.n_r, 0, oper.ofmDim.rows - 1, oper.ofmDim.n_c, 0, oper.ofmDim.cols - 1}});
        }
    }

    // === 연결(Connection) 추가 ===
    df->addConn(Connection("", "WordEmbedding", true, false, data_dram[0][0], "WE input"));
    df->addConn(Connection("WordEmbedding", "", false, true, data_dram[0][2], "WE output"));

    df->addConn(Connection("", "FC-Q", true, false, data_dram[1][1], "FC-Q weight"));
    df->addConn(Connection("FC-Q", "", false, true, data_dram[1][2], "FC-Q output"));
    df->addConn(Connection("WordEmbedding", "FC-Q", false, false, -1, "activations"));

    df->addConn(Connection("", "FC-V", true, false, data_dram[2][1], "FC-V weight"));
    df->addConn(Connection("FC-V", "", false, true, data_dram[2][2], "FC-V output"));
    df->addConn(Connection("WordEmbedding", "FC-V", false, false, -1, "activations"));

    for (int i = 0; i < 8; i++)
    {
        std::string fckName = "FC-K" + std::to_string(i);
        df->addConn(Connection("", fckName, true, false, data_dram[3 + i * 2][1], "FC-K weight"));
        df->addConn(Connection("WordEmbedding", fckName, false, false, -1, "activations"));
    }

    for (int i = 0; i < 8; i++)
    {
        std::string fckName = "FC-K" + std::to_string(i);
        std::string tpkName = "Transpose-K" + std::to_string(i);
        df->addConn(Connection(fckName, tpkName, false, false, -1, "activations"));
    }

    for (int i = 0; i < 8; i++)
    {
        std::string tpkName = "Transpose-K" + std::to_string(i);
        df->addConn(Connection(tpkName, "Merge-K", false, false, -1, "activations"));
    }

    df->addConn(Connection("Merge-K", "", false, true, data_dram[19][2], "Merge-K final output"));

    // 자동 partition 생성 (각 연산의 weight partition을 생성)
    for (auto &op : df->ops)
    {
        autoGeneratePartitions(op);
    }
}

unsigned long long gemini_plus::get_perf_recursive()
{
    df->hop_cnt.clear();
    df->num_max_mcast_port = 0;
    df->src_cnt.clear();

    int batch_size = 64;
    int pipeline_depth = 3;
    int num_pass = (batch_size + (pipeline_depth - 1) * 4);
    int tot_dly = 0;
    long long hop_cnt_pass8 = 0;
    int num_act_link = 0;
    unsigned long long max_comm_dly = 0;

    std::vector<std::pair<noc_pos_t, noc_pos_t>> dominant_link;
    std::vector<std::pair<unsigned long long, unsigned long long>> pass_time;
    for (int pass = 0; pass < 9; pass++)
    {
        df->max_comp_dly = 0;
        df->hop_cnt.clear();
        assert(df->hop_cnt.empty());
        df->calc_NoCTraffic_recursive(allDRAMs, pass);
        df->calc_CompDly(pass);
        auto max_link = std::max_element(df->hop_cnt.begin(), df->hop_cnt.end(), [](const auto &x, const auto &y)
                                         { return x.second < y.second; });
        max_comm_dly = max_link->second;

        pass_time.push_back(std::make_pair(df->max_comp_dly, max_comm_dly));
        if (df->max_comp_dly > max_comm_dly)
            tot_dly += df->max_comp_dly;
        else
            tot_dly += max_comm_dly;

        if (pass == 8)
        {
            dominant_link.push_back(max_link->first);
            num_act_link = df->hop_cnt.size();
        }

        // std::cout << "Pass: " << pass << std::endl;
        // std::cout << "--- Flow (F_(s,i)) ---" << std::endl;
        // for(auto f : df->f_s_i)
        // {
        //     int i = 0;
        //     for(auto d_n_v : f.second)
        //     {
        //         std::cout << "f_(" << f.first << "," << i << "): ";
        //         std::cout << "D_(" << f.first << "," << i << ")={";
        //         for(auto d : d_n_v.first)
        //         {
        //             std::cout << d;
        //             if(d != d_n_v.first.back())
        //                 std::cout << ",";
        //         }
        //         std::cout << "}";
        //         std::cout << ", V_(" << f.first << "," << i << ")=" << d_n_v.second;
        //         std::cout << std::endl;
        //         i++;
        //     }
        // }

        // std::cout << "--- Routing(E_f) --- " << std::endl;
        // for(auto E_f : df->E_f_s_i)
        // {
        //     std::cout<< "E_f_(" << E_f.first.first << "," << E_f.first.second << ")={";
        //     for(auto e : E_f.second)
        //     {
        //         PRINT_POS(std::cout, e.first);
        //         std::cout << "->";
        //         PRINT_POS(std::cout, e.second);
        //         std::cout <<",";
        //     }
        //     std::cout << "}";
        //     std::cout << std::endl;
        // }

        df->f_s_i.clear();
        df->E_f_s_i.clear();
    }
    for (auto h : df->hop_cnt)
    {
        hop_cnt_pass8 += h.second * 55;
    }
    if (df->max_comp_dly > max_comm_dly)
        tot_dly += df->max_comp_dly * 55;
    else
        tot_dly += max_comm_dly * 55;

    for (int pass = 64; pass < num_pass; pass++)
    {
        df->max_comp_dly = 0;
        df->hop_cnt.clear();
        assert(df->hop_cnt.empty());
        df->calc_NoCTraffic_recursive(allDRAMs, pass);
        df->calc_CompDly(pass);
        auto max_link = std::max_element(df->hop_cnt.begin(), df->hop_cnt.end(), [](const auto &x, const auto &y)
                                         { return x.second < y.second; });
        max_comm_dly = max_link->second;
        for (auto h : df->hop_cnt)
        {
            hop_cnt_pass8 += h.second;
        }

        pass_time.push_back(std::make_pair(df->max_comp_dly, max_comm_dly));
        if (df->max_comp_dly > max_link->second)
            tot_dly += df->max_comp_dly;
        else
            tot_dly += max_comm_dly;
    }

    return tot_dly;
}

unsigned long long gemini_plus::get_perf_loop()
{
    // std::cout << "get_perf_loop()" << std::endl;
    df->hop_cnt.clear();
    df->num_max_mcast_port = 0;
    df->src_cnt.clear();

    int batch_size = 64;
    int pipeline_depth = 3;
    int num_pass = (batch_size + (pipeline_depth - 1) * 4);
    int tot_dly = 0;
    unsigned long long tot_dly_eff = 0;
    unsigned long long dly_eff = 0;
    long long hop_cnt_pass8 = 0;
    int num_act_link = 0;
    unsigned long long max_comm_dly = 0;

    std::vector<std::pair<noc_pos_t, noc_pos_t>> dominant_link;
    std::vector<std::pair<unsigned long long, unsigned long long>> pass_time;
    for (int pass = 0; pass < 9; pass++)
    {
        df->max_comp_dly = 0;
        df->hop_cnt.clear();
        assert(df->hop_cnt.empty());
        // std::cout << "calc_NoCTraffic_loop()" << std::endl;
        df->calc_NoCTraffic_loop(allDRAMs, pass);
        // std::cout << "calc_CompDly()" << std::endl;
        df->calc_CompDly(pass);
        auto max_link = std::max_element(df->hop_cnt.begin(), df->hop_cnt.end(), [](const auto &x, const auto &y)
                                         { return x.second < y.second; });
        max_comm_dly = max_link->second;

        pass_time.push_back(std::make_pair(df->max_comp_dly, max_comm_dly));
        if (df->max_comp_dly > max_comm_dly)
            tot_dly += df->max_comp_dly;
        else
            tot_dly += max_comm_dly;

        if (pass == 8)
        {
            dominant_link.push_back(max_link->first);
            num_act_link = df->hop_cnt.size();
        }

        // dly_eff = calc_effBW();
        // std::cout << "calc_upper()" << std::endl;
        dly_eff = calc_upper();
        tot_dly_eff += dly_eff;

        df->f_s_i.clear();
        df->E_f_s_i.clear();
    }
    for (auto h : df->hop_cnt)
    {
        hop_cnt_pass8 += h.second * 55;
    }
    if (df->max_comp_dly > max_comm_dly)
        tot_dly += df->max_comp_dly * 55;
    else
        tot_dly += max_comm_dly * 55;

    tot_dly_eff += dly_eff * 55;

    for (int pass = 64; pass < num_pass; pass++)
    {
        df->max_comp_dly = 0;
        df->hop_cnt.clear();
        assert(df->hop_cnt.empty());
        df->calc_NoCTraffic_loop(allDRAMs, pass);
        df->calc_CompDly(pass);
        auto max_link = std::max_element(df->hop_cnt.begin(), df->hop_cnt.end(), [](const auto &x, const auto &y)
                                         { return x.second < y.second; });
        max_comm_dly = max_link->second;
        for (auto h : df->hop_cnt)
        {
            hop_cnt_pass8 += h.second;
        }

        pass_time.push_back(std::make_pair(df->max_comp_dly, max_comm_dly));
        if (df->max_comp_dly > max_link->second)
            tot_dly += df->max_comp_dly;
        else
            tot_dly += max_comm_dly;

        // dly_eff = calc_effBW();
        dly_eff = calc_upper();
        tot_dly_eff += dly_eff;
    }
    return tot_dly_eff;
}

unsigned long long gemini_plus::calc_effBW()
{
    auto eff_tbl_ct = df->eff_tbl_ct;
    auto eff_tbl_wh = df->eff_tbl_wh;
    auto eff_tbl_bw = df->tbl_bw;
    auto eff_E_f_s_i = df->eff_E_f_s_i;

    std::vector<std::string> str_dir = {"L", "N", "E", "S", "W"};

    int tot_dly = 0;
#ifdef DBG
    std::cout << "F_WH_ONLY" << std::endl;
    for (auto f : df->f_wh_only)
    {
        PRINT_FLOW(std::cout, f);
        std::cout << std::endl;
    }
    std::cout << "CALC START" << std::endl;
#endif
    while (!eff_E_f_s_i.empty())
    {
        eff_tbl_bw = df->tbl_bw;

        std::map<int, std::vector<flow_idx_t>> E_f_sz;
        for (auto E_f : eff_E_f_s_i)
        {
            if (df->f_wh_only.find(E_f.first) == df->f_wh_only.end())
                E_f_sz[E_f.second.size()].push_back(E_f.first);
        }
        std::vector<std::pair<flow_idx_t, std::map<link_t, double>>> tmp_eff_E_f_s_i;

        for (auto E_f : E_f_sz)
        {
            for (auto f : E_f.second)
                tmp_eff_E_f_s_i.push_back({f, eff_E_f_s_i[f]});
        }
        std::reverse(tmp_eff_E_f_s_i.begin(), tmp_eff_E_f_s_i.end());

        for (auto wh : df->f_wh_only)
        {
            tmp_eff_E_f_s_i.push_back({wh, eff_E_f_s_i[wh]});
        }

        // auto tmp_eff_E_f_s_i = eff_E_f_s_i;

        // Multicast source blocked
        for (auto E_f : tmp_eff_E_f_s_i)
        {
            flow_idx_t src_f_idx = E_f.first;
            noc_pos_t src_pos = rid2pos(src_f_idx.first, 1);
// #ifdef DBG
//             PRINT_FLOW(std::cout, src_f_idx);
// #endif
            for (auto E_s : E_f.second)
            {
                // source to N/E/S/W
                // upstream link start == source
                link_t cmp_link = E_s.first;
                noc_pos_t cmp_src_pos = cmp_link.first;
                if (cmp_src_pos.second == 0)
                    continue;

                if (src_pos == cmp_src_pos)
                {
                    bool del_f = false;
                    if (eff_tbl_ct[cmp_link].find(src_f_idx) != eff_tbl_ct[cmp_link].end())
                    {
                        if (eff_tbl_wh[cmp_link].size() > 0)
                        {
                            del_f = true;
                        }

                        if (del_f)
                        {
#ifdef DBG
                            PRINT_FLOW(std::cout, src_f_idx);
                            std::cout << " is BLOCKED BY ";
                            PRINT_LINK(std::cout, cmp_link);
                            std::cout << std::endl;
#endif
                            for (auto &ct : eff_tbl_ct)
                            {
                                ct.second.erase(src_f_idx);
                            }
                            for (auto &wh : eff_tbl_wh)
                            {
                                wh.second.erase(src_f_idx);
                            }
                            eff_E_f_s_i.erase(src_f_idx);

                            break;
                        }
                    }
                }
            }
// #ifdef DBG
//             std::cout << std::endl;
// #endif
        }

        // Cut-through link blocked by Wormhole traffic
        for (auto E_f : tmp_eff_E_f_s_i)
        {
            flow_idx_t src_f_idx = E_f.first;
            noc_pos_t src_pos = rid2pos(src_f_idx.first, 1);
// #ifdef DBG
//             PRINT_FLOW(std::cout, src_f_idx);
// #endif
            for (auto E_s : E_f.second)
            {
                std::deque<std::pair<noc_pos_t, noc_pos_t>> hop;

                hop.push_back(E_s.first);
                bool isBlocked = false;
                for (auto h = hop.begin(); h != hop.end(); h++)
                {
                    link_t cmp_link = *h;
                    noc_pos_t cmp_src_pos = cmp_link.first;
                    if (eff_tbl_ct[cmp_link].find(E_f.first) != eff_tbl_ct[cmp_link].end())
                    {
                        isBlocked = false;
                        for (auto wh : eff_tbl_wh[cmp_link])
                        {

                            if (df->f_wh_only.find(wh) != df->f_wh_only.end())
                            {
                                isBlocked = true;
#ifdef DBG
                                PRINT_FLOW(std::cout, src_f_idx);
                                std::cout << " is BLOCKED BY ";
                                PRINT_LINK(std::cout, cmp_link);
                                std::cout << std::endl;
#endif
                                break;
                            }
                            else if (eff_E_f_s_i[wh][cmp_link] <= eff_E_f_s_i[E_f.first][cmp_link])
                            {
                                isBlocked = true;
#ifdef DBG
                                PRINT_FLOW(std::cout, src_f_idx);
                                std::cout << " is BLOCKED BY ";
                                PRINT_LINK(std::cout, cmp_link);
                                std::cout << std::endl;
#endif
                                break;
                            }
                        }
                    }

                    if (isBlocked)
                        break;
                    for (auto e : E_f.second)
                    {
                        if (cmp_link.second == e.first.first)
                        {
                            hop.push_back(e.first);
                        }
                    }
                }

                if (isBlocked)
                {
                    for (auto &ct : eff_tbl_ct)
                    {
                        ct.second.erase(E_f.first);
                    }
                    for (auto &wh : eff_tbl_wh)
                    {
                        wh.second.erase(E_f.first);
                    }
                    eff_E_f_s_i.erase(E_f.first);
                }
            }
// #ifdef DBG
//             std::cout << std::endl;
// #endif
        }

#ifdef DBG
        std::cout << "Effective BW Calculation" << std::endl;
        
        std::cout << "Check DRAM BW" << std::endl;
#endif
        std::map<int, int> dram_contention;
        noc_pos_t src, dst;
        for (int y = 0; y < 6; y++)
        {
            
            int src_x = -1;
            src = {{src_x, y}, 0};
            dst = {{src_x, y}, 1};
            link_t l = {src, dst};

            if (!eff_tbl_ct[l].empty())
            {
                if (y < 3)
                    dram_contention[1]++;
                else
                    dram_contention[0]++;
            }
            if (!eff_tbl_wh[l].empty())
            {
                if (y < 3)
                    dram_contention[1]++;
                else
                    dram_contention[0]++;
            }

            src_x = 6;
            src = {{src_x, y}, 0};
            dst = {{src_x, y}, 1};
            l = {src, dst};
            if (!eff_tbl_ct[l].empty())
            {
                if (y < 3)
                    dram_contention[3]++;
                else
                    dram_contention[2]++;
            }
            if (!eff_tbl_wh[l].empty())
            {
                if (y < 3)
                    dram_contention[3]++;
                else
                    dram_contention[2]++;
            }
        }
#ifdef DBG
        for (auto d : dram_contention)
        {
            std::cout << "DRAM " << d.first << ": " << d.second << std::endl;
        }
#endif

#ifdef DBG
        std::cout << "BW init" << std::endl;
#endif
        for (auto &E_f : eff_E_f_s_i)
        {
#ifdef DBG
            PRINT_FLOW(std::cout, E_f.first);
            std::cout << std::endl;
#endif
            for (auto &l : E_f.second)
            {
                noc_pos_t src_pos = rid2pos(E_f.first.first, 1);
                int src_x = src_pos.first.first;
                int src_y = src_pos.first.second;
                if (src_x == -1)
                {
                    int src_y = rid2pos(E_f.first.first, 1).first.second;
                    if (src_y < 3)
                        l.second = (32 / dram_contention[1]);
                    else
                        l.second = (32 / dram_contention[0]);
                }
                else if (src_x == 6)
                {
                    int src_y = rid2pos(E_f.first.first, 1).first.second;
                    if (src_y < 3)
                        l.second = (32 / dram_contention[3]);
                    else
                        l.second = (32 / dram_contention[2]);
                }
                else
                    l.second = df->tbl_bw[l.first];

                if (l.first.first.second == 0)
                    l.second /= (eff_tbl_ct[l.first].size() + eff_tbl_wh[l.first].size());
                
                if (l.first.second.second == 0)
                    l.second /= (eff_tbl_ct[l.first].size() + eff_tbl_wh[l.first].size());
            }
        }

        std::map<NOC_DIR, std::set<flow_idx_t>> in_traffic;
        double divisor = 0;
        for (int pos_y = 0; pos_y < 6; pos_y++)
        {

#ifdef DBG
            std::cout << "LEFT -> RIGHT (y=" << pos_y << ")" << std::endl;
#endif
            // Left to Right
            for (int pos_x = -1; pos_x < 6; pos_x++)
            {
                link_t l_cur = {{{pos_x, pos_y}, 1}, {{pos_x + 1, pos_y}, 1}};
                if (eff_tbl_ct[l_cur].empty() && eff_tbl_wh[l_cur].empty())
                    continue;
#ifdef DBG
                PRINT_LINK(std::cout, l_cur);
                std::cout << ": ";
#endif
                for (auto ct : eff_tbl_ct[l_cur])
                {
                    noc_pos_t src = rid2pos(ct.first, 1);
                    if (src == l_cur.first)
                        in_traffic[NOC_DIR::L].insert(ct);
                    else
                        in_traffic[NOC_DIR::W].insert(ct);
                }

                for (auto wh : eff_tbl_wh[l_cur])
                {
                    noc_pos_t src = rid2pos(wh.first, 1);
                    if (src == l_cur.first)
                        in_traffic[NOC_DIR::L].insert(wh);
                    else
                        in_traffic[NOC_DIR::W].insert(wh);
                }

                divisor = 0;
                for (auto dir : in_traffic)
                {
#ifdef DBG
                    std::cout << str_dir[(int)dir.first] << "(" << dir.second.size() << ") / ";
#endif
                    divisor += 1;
                }
                double base_bw = df->tbl_bw[l_cur] / divisor;
#ifdef DBG
                std::cout << "BW: " << df->tbl_bw[l_cur] << " => " << base_bw << std::endl;
#endif
                for (auto dir : in_traffic)
                {
#ifdef DBG
                    std::cout << str_dir[(int)dir.first] << " | ";
#endif
                    for (auto f : dir.second)
                    {
#ifdef DBG
                        PRINT_FLOW(std::cout, f);
                        std::cout << ": " << base_bw << " => " << base_bw / dir.second.size() << " / ";
#endif
                        eff_E_f_s_i[f][l_cur] = base_bw / dir.second.size();
                    }
#ifdef DBG
                    std::cout << std::endl;
#endif
                }
                in_traffic.clear();
            }
#ifdef DBG
            std::cout << std::endl;

            std::cout << "RIGHT -> LEFT (y=" << pos_y << ")" << std::endl;
#endif
            for (int pos_x = 6; pos_x > -1; pos_x--)
            {
                link_t l_cur = {{{pos_x, pos_y}, 1}, {{pos_x - 1, pos_y}, 1}};
                if (eff_tbl_ct[l_cur].empty() && eff_tbl_wh[l_cur].empty())
                    continue;
#ifdef DBG
                PRINT_LINK(std::cout, l_cur);
                std::cout << ": ";
#endif
                for (auto ct : eff_tbl_ct[l_cur])
                {
                    noc_pos_t src = rid2pos(ct.first, 1);
                    if (src == l_cur.first)
                        in_traffic[NOC_DIR::L].insert(ct);
                    else
                        in_traffic[NOC_DIR::E].insert(ct);
                }

                for (auto wh : eff_tbl_wh[l_cur])
                {
                    noc_pos_t src = rid2pos(wh.first, 1);
                    if (src == l_cur.first)
                        in_traffic[NOC_DIR::L].insert(wh);
                    else
                        in_traffic[NOC_DIR::E].insert(wh);
                }

                divisor = 0;
                for (auto dir : in_traffic)
                {
#ifdef DBG
                    std::cout << str_dir[(int)dir.first] << "(" << dir.second.size() << ") / ";
#endif
                    divisor += 1;
                }
                double base_bw = df->tbl_bw[l_cur] / divisor;
#ifdef DBG
                std::cout << "BW: " << df->tbl_bw[l_cur] << " => " << base_bw << std::endl;
#endif

                for (auto dir : in_traffic)
                {
#ifdef DBG
                    std::cout << str_dir[(int)dir.first] << " | ";
#endif
                    for (auto f : dir.second)
                    {
#ifdef DBG
                        PRINT_FLOW(std::cout, f);
                        std::cout << ": " << base_bw << " => " << base_bw / dir.second.size() << " / ";
#endif
                        eff_E_f_s_i[f][l_cur] = base_bw / dir.second.size();
                    }
#ifdef DBG
                    std::cout << std::endl;
#endif
                }

                in_traffic.clear();
            }
#ifdef DBG
            std::cout << std::endl;
#endif
        }

        for (int pos_x = -1; pos_x < 6; pos_x++)
        {
// Top to bottom
#ifdef DBG
            std::cout << "TOP -> BOTTOM (x=" << pos_x << ")" << std::endl;
#endif
            for (int pos_y = 0; pos_y < 5; pos_y++)
            {
                link_t l_cur = {{{pos_x, pos_y}, 1}, {{pos_x, pos_y + 1}, 1}};
                if (eff_tbl_ct[l_cur].empty() && eff_tbl_wh[l_cur].empty())
                    continue;
#ifdef DBG
                PRINT_LINK(std::cout, l_cur);
                std::cout << ": ";
#endif
                for (auto ct : eff_tbl_ct[l_cur])
                {
                    noc_pos_t src = rid2pos(ct.first, 1);
                    int src_x = src.first.first;
                    int src_y = src.first.second;
                    int cur_x = l_cur.first.first.first;
                    int cur_y = l_cur.first.first.second;
                    if (src == l_cur.first)
                        in_traffic[NOC_DIR::L].insert(ct);
                    else if (src_y < cur_y)
                        in_traffic[NOC_DIR::N].insert(ct);
                    else if (src_x > cur_x)
                        in_traffic[NOC_DIR::E].insert(ct);
                    else if (src_x < cur_x)
                        in_traffic[NOC_DIR::W].insert(ct);
                }

                for (auto wh : eff_tbl_wh[l_cur])
                {
                    noc_pos_t src = rid2pos(wh.first, 1);
                    int src_x = src.first.first;
                    int src_y = src.first.second;
                    int cur_x = l_cur.first.first.first;
                    int cur_y = l_cur.first.first.second;
                    if (src == l_cur.first)
                        in_traffic[NOC_DIR::L].insert(wh);
                    else if (src_y < cur_y)
                        in_traffic[NOC_DIR::N].insert(wh);
                    else if (src_x > cur_x)
                        in_traffic[NOC_DIR::E].insert(wh);
                    else if (src_x < cur_x)
                        in_traffic[NOC_DIR::W].insert(wh);
                }

                divisor = 0;
                for (auto dir : in_traffic)
                {
#ifdef DBG
                    std::cout << str_dir[(int)dir.first] << "(" << dir.second.size() << ") / ";
#endif
                    divisor += 1;
                }
                double base_bw = df->tbl_bw[l_cur] / divisor;
#ifdef DBG
                std::cout << "BW: " << df->tbl_bw[l_cur] << " => " << base_bw << std::endl;
#endif

                for (auto dir : in_traffic)
                {
#ifdef DBG
                    std::cout << str_dir[(int)dir.first] << " | ";
#endif
                    for (auto f : dir.second)
                    {
#ifdef DBG
                        PRINT_FLOW(std::cout, f);
                        std::cout << ": " << base_bw << " => " << base_bw / dir.second.size() << " / ";
#endif
                        eff_E_f_s_i[f][l_cur] = base_bw / dir.second.size();
                    }
#ifdef DBG
                    std::cout << std::endl;
#endif
                }
                in_traffic.clear();
            }
#ifdef DBG
            std::cout << std::endl;

            // Top to bottom
            std::cout << "BOTTOM -> TOP (x=" << pos_x << ")" << std::endl;
#endif
            for (int pos_y = 5; pos_y > 0; pos_y--)
            {
                link_t l_cur = {{{pos_x, pos_y}, 1}, {{pos_x, pos_y - 1}, 1}};
                if (eff_tbl_ct[l_cur].empty() && eff_tbl_wh[l_cur].empty())
                    continue;
#ifdef DBG
                PRINT_LINK(std::cout, l_cur);
                std::cout << ": ";
#endif
                for (auto ct : eff_tbl_ct[l_cur])
                {
                    noc_pos_t src = rid2pos(ct.first, 1);
                    int src_x = src.first.first;
                    int src_y = src.first.second;
                    int cur_x = l_cur.first.first.first;
                    int cur_y = l_cur.first.first.second;
                    if (src == l_cur.first)
                        in_traffic[NOC_DIR::L].insert(ct);
                    else if (src_y > cur_y)
                        in_traffic[NOC_DIR::S].insert(ct);
                    else if (src_x < cur_x)
                        in_traffic[NOC_DIR::E].insert(ct);
                    else if (src_x > cur_x)
                        in_traffic[NOC_DIR::W].insert(ct);
                }

                for (auto wh : eff_tbl_wh[l_cur])
                {
                    noc_pos_t src = rid2pos(wh.first, 1);
                    int src_x = src.first.first;
                    int src_y = src.first.second;
                    int cur_x = l_cur.first.first.first;
                    int cur_y = l_cur.first.first.second;
                    if (src == l_cur.first)
                        in_traffic[NOC_DIR::L].insert(wh);
                    else if (src_y > cur_y)
                        in_traffic[NOC_DIR::S].insert(wh);
                    else if (src_x < cur_x)
                        in_traffic[NOC_DIR::E].insert(wh);
                    else if (src_x > cur_x)
                        in_traffic[NOC_DIR::W].insert(wh);
                }

                divisor = 0;
                for (auto dir : in_traffic)
                {
#ifdef DBG
                    std::cout << str_dir[(int)dir.first] << "(" << dir.second.size() << ") / ";
#endif
                    divisor += 1;
                }
                double base_bw = df->tbl_bw[l_cur] / divisor;
#ifdef DBG
                std::cout << "BW: " << df->tbl_bw[l_cur] << " => " << base_bw << std::endl;
#endif

                for (auto dir : in_traffic)
                {
#ifdef DBG
                    std::cout << str_dir[(int)dir.first] << " | ";
#endif
                    for (auto f : dir.second)
                    {
#ifdef DBG
                        PRINT_FLOW(std::cout, f);
                        std::cout << ": " << base_bw << " => " << base_bw / dir.second.size() << " / ";
#endif
                        eff_E_f_s_i[f][l_cur] = base_bw / dir.second.size();
                    }
#ifdef DBG
                    std::cout << std::endl;
#endif
                }
                in_traffic.clear();
            }
#ifdef DBG
            std::cout << std::endl;
#endif
        }

        std::map<flow_idx_t, std::pair<int, double>> f_dly_bw;
        link_t min_link;
        int min_dly = 0;
        for (auto e : eff_E_f_s_i)
        {
            double min_bw = 32;
            for (auto l : e.second)
            {
                if (l.second < min_bw)
                {
                    min_bw = l.second;
                    min_link = l.first;
                }
            }
            int dly = ceil((double)df->f_s_i[e.first].second / min_bw);
            f_dly_bw[e.first] = {dly, min_bw};

            if (min_dly == 0)
                min_dly = dly;
            else if (dly < min_dly)
                min_dly = dly;
#ifdef DBG
            PRINT_FLOW(std::cout, e.first);
            std::cout.precision(10);
            std::cout << ": " << df->f_s_i[e.first].second << " / " << min_bw << " = " << dly << " | ";
            PRINT_LINK(std::cout, min_link);
            std::cout << std::endl;
#endif
        }
#ifdef DBG
        std::cout << "finished flow: ";
#endif

        for (auto dly : f_dly_bw)
        {
            bool isFinished = false;
            if (dly.second.first == min_dly)
            {
#ifdef DBG
                PRINT_FLOW(std::cout, dly.first);
                std::cout << " / ";
#endif
                isFinished = true;
            }
            else
            {
                if (df->f_s_i[dly.first].second > min_dly * dly.second.second)
                {
                    isFinished = false;
                }
                else
                {
                    isFinished = true;
                }
            }

            if (isFinished)
            {
                for (auto &ct : eff_tbl_ct)
                {
                    df->tbl_ct[ct.first].erase(dly.first);
                    ct.second.erase(dly.first);
                }

                for (auto &wh : eff_tbl_wh)
                {
                    df->tbl_wh[wh.first].erase(dly.first);
                    wh.second.erase(dly.first);
                }

                eff_E_f_s_i.erase(dly.first);
                df->E_f_s_i.erase(dly.first);
                df->f_wh_only.erase(dly.first);
            }
            else
            {
                df->f_s_i[dly.first].second -= min_dly * dly.second.second;
            }
        }
#ifdef DBG
        std::cout << std::endl;
#endif

        for (auto e : df->E_f_s_i)
        {
            bool remain_flow = false;
            if (eff_E_f_s_i.find(e.first) != eff_E_f_s_i.end())
                continue;

            for (auto eff_e : eff_E_f_s_i)
            {
                if (eff_e.first.first == e.first.first)
                {
                    if (eff_e.first.second.first == e.first.second.first)
                    {
                        remain_flow = false;
                    }
                    else
                    {
                        remain_flow = true;
                        break;
                    }
                }
            }
            if (remain_flow)
                continue;
#ifdef DBG
            std::cout << "INSERT: ";
            PRINT_FLOW(std::cout, e.first);
            std::cout << ".vol=" << df->f_s_i[e.first].second;
            std::cout << "\n";
#endif

            eff_E_f_s_i.insert(e);
            for (auto l : e.second)
            {
                if (df->tbl_ct[l.first].find(e.first) != df->tbl_ct[l.first].end())
                {
                    // std::cout << "INSERT CT" << std::endl;
                    eff_tbl_ct[l.first].insert(e.first);
                }

                if (df->tbl_wh[l.first].find(e.first) != df->tbl_wh[l.first].end())
                {
                    // std::cout << "INSERT WH" << std::endl;
                    eff_tbl_wh[l.first].insert(e.first);
                }
            }
        }
#ifdef DBG
        std::cout << "Cut-Through Traffics" << std::endl;
        for(auto ct : eff_tbl_ct)
        {
            if(ct.second.empty())
                continue;
            PRINT_LINK(std::cout, ct.first);
            std::cout << ": ";
            for(auto f : ct.second)
            {
                PRINT_FLOW(std::cout, f);
                std::cout << " / ";
            }
            std::cout << std::endl;
        }
        std::cout << std::endl;

        std::cout << "Worm-hole Traffics" << std::endl;
        for(auto wh : eff_tbl_wh)
        {
            if(wh.second.empty())
                continue;
            PRINT_LINK(std::cout, wh.first);
            std::cout << ": ";
            for(auto f : wh.second)
            {
                PRINT_FLOW(std::cout, f);
                std::cout << " / ";
            }
            std::cout << std::endl;
        }
#endif

#ifdef DBG
        std::cout << "=> tot_dly: ";
        std::cout << std::setw(6) << tot_dly << " + ";
        std::cout << std::setw(6) << min_dly << " = ";
        std::cout << std::setw(6) << tot_dly + min_dly << std::endl;
#endif
        tot_dly += min_dly;
    }

    return tot_dly;
}

std::pair<unsigned long long, unsigned long long> gemini_plus::get_perf_both()
{
    df->hop_cnt.clear();
    df->num_max_mcast_port = 0;
    df->src_cnt.clear();

    int batch_size = 64;
    int pipeline_depth = 3;
    int num_pass = (batch_size + (pipeline_depth - 1) * 4);
    int tot_dly = 0;
    unsigned long long tot_dly_eff = 0;
    unsigned long long dly_eff = 0;
    long long hop_cnt_pass8 = 0;
    int num_act_link = 0;
    unsigned long long max_comm_dly = 0;
    unsigned long long pass_dly;

    std::vector<std::pair<noc_pos_t, noc_pos_t>> dominant_link;
    std::vector<std::pair<unsigned long long, unsigned long long>> pass_time;
    for (int pass = 0; pass < 9; pass++)
    {
        df->max_comp_dly = 0;
        df->hop_cnt.clear();
        assert(df->hop_cnt.empty());
        df->calc_NoCTraffic_loop(allDRAMs, pass);
        df->calc_CompDly(pass);
        auto max_link = std::max_element(df->hop_cnt.begin(), df->hop_cnt.end(), [](const auto &x, const auto &y)
                                         { return x.second < y.second; });
        max_comm_dly = max_link->second;

        pass_time.push_back(std::make_pair(df->max_comp_dly, max_comm_dly));

        if (df->max_comp_dly > max_comm_dly)
        {
            pass_dly = df->max_comp_dly;
            tot_dly += df->max_comp_dly;
        }
        else
        {
            pass_dly = max_comm_dly;
            tot_dly += max_comm_dly;
        }

        if (pass == 8)
        {
            dominant_link.push_back(max_link->first);
            num_act_link = df->hop_cnt.size();
        }
#ifdef DBG
        std::cout << "Pass " << pass << std::endl;
#endif
        dly_eff = calc_effBW();
        // if(dly_eff < pass_dly)
        //     dly_eff = pass_dly;
        // dly_eff = calc_upper();
        tot_dly_eff += dly_eff;

        // std::cout << dly_eff << std::endl;
        df->f_s_i.clear();
        df->E_f_s_i.clear();
    }
    for (auto h : df->hop_cnt)
    {
        hop_cnt_pass8 += h.second * 55;
    }
    if (df->max_comp_dly > max_comm_dly)
        tot_dly += df->max_comp_dly * 55;
    else
        tot_dly += max_comm_dly * 55;

    tot_dly_eff += dly_eff * 55;

    for (int pass = 64; pass < num_pass; pass++)
    {
        df->max_comp_dly = 0;
        df->hop_cnt.clear();
        assert(df->hop_cnt.empty());
        df->calc_NoCTraffic_loop(allDRAMs, pass);
        df->calc_CompDly(pass);
        auto max_link = std::max_element(df->hop_cnt.begin(), df->hop_cnt.end(), [](const auto &x, const auto &y)
                                         { return x.second < y.second; });
        max_comm_dly = max_link->second;
        for (auto h : df->hop_cnt)
        {
            hop_cnt_pass8 += h.second;
        }

        pass_time.push_back(std::make_pair(df->max_comp_dly, max_comm_dly));
        if (df->max_comp_dly > max_comm_dly)
        {
            pass_dly = df->max_comp_dly;
            tot_dly += df->max_comp_dly;
        }
        else
        {
            pass_dly = max_comm_dly;
            tot_dly += max_comm_dly;
        }

#ifdef DBG
        std::cout << "Pass " << pass << std::endl;
#endif
        dly_eff = calc_effBW();
        // if(dly_eff < pass_dly)
        //     dly_eff = pass_dly;
        // dly_eff = calc_upper();
        tot_dly_eff += dly_eff;
    }
    // std::cout << "TOT_DLY_EFF: " << tot_dly_eff << std::endl;
    return std::make_pair(tot_dly, tot_dly_eff);
}

unsigned long long gemini_plus::calc_upper()
{

    auto f_s_i = df->f_s_i;
    auto E_f_s_i = df->E_f_s_i;
    auto tbl_bw = df->tbl_bw;
    auto tbl_f = df->tbl_f;

    std::map<link_t, double> tbl_dly;
    for (auto &E_f : E_f_s_i)
    {
        auto f_vol = f_s_i[E_f.first].second;
        double min_bw = 100;

        for (auto &e : E_f.second)
        {
            auto bw = tbl_bw[e.first];
            e.second = f_vol / bw;
            tbl_dly[e.first] += e.second;

            // auto bw = tbl_bw[e.first];
            // if (bw < min_bw)
            //     min_bw = bw;
        }

        // for(auto &e : E_f.second)
        // {
        //     e.second = f_vol / min_bw;
        //     tbl_dly[e.first] += e.second;
        // }
    }

    double max_dly = 0;
    for (auto dly : tbl_dly)
    {
        if (dly.second > max_dly)
            max_dly = dly.second;
    }

    return (unsigned long long)ceil(max_dly);
}
