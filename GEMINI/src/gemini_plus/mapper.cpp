#include "gemini_plus/mapper.h"



noc_pos_t rid2pos(int rid, int lv)
{
    int pos_x;
    int pos_y;

    if (rid - 6 < 0)
        pos_x = -1;
    else
        pos_x = (rid - 6) / 6;

    pos_y = (6 - 1) - rid % 6;

    noc_pos_t pos = {{pos_x, pos_y}, lv};

    return pos;
}

int cmpntid2rid(int cmpntid)
{
    int rid;
    // DRAM
    if (cmpntid < 4)
    {
        if (cmpntid < 2)
            rid = cmpntid * 3;
        else
            rid = 36 + cmpntid * 3;
    }
    else
    {
        rid = (cmpntid - 4) + 6;
    }
    return rid;
}

int dpathid2rid(int dpathid)
{
    return dpathid + 6;
}

DataFlow::DataFlow(){
    hop_cnt.clear();
    src_cnt.clear();
    from_src.clear();
    is_mcast_link.clear();
};

void DataFlow::addOp(const Operation &op)
{
    ops.push_back(op);
}

void DataFlow::addConn(const Connection &c)
{
    conns.push_back(c);
}

void DataFlow::calc_unicast(int src, int dst, int size, int root)
{
    int x_dir;
    int y_dir;

    int lv;
    if(src == root)
        lv = 0;
    else
        lv = 1;
    noc_pos_t pos_src = rid2pos(src, 0);    // TODO
    noc_pos_t pos_dst = rid2pos(dst, 0);    // TODO

// PRINT_POS(pos_src);
#ifdef LOG_ALL
    std::cout << " -> ";
    PRINT_POS(pos_dst);
#endif

    if (pos_src.first > pos_dst.first)
        x_dir = -1;
    else if (pos_src.first < pos_dst.first)
        x_dir = 1;
    else
        x_dir = 0;

    if (pos_src.second > pos_dst.second)
        y_dir = -1;
    else if (pos_src.second < pos_dst.second)
        y_dir = 1;
    else
        y_dir = 0;

    noc_pos_t hop_src, hop_dst;
    hop_src = pos_src;
    hop_dst.second = hop_src.second;
    // X direction
    while (hop_src.first != pos_dst.first)
    {
        hop_dst.first.first = hop_src.first.first + x_dir;  // TODO
        std::pair<noc_pos_t, noc_pos_t> hop_src_dst = {hop_src, hop_dst};

        // std::cout << " (" << std::setw(2) << hop_src.first << "," << std::setw(2) << hop_src.second << ") -> ";
        // PRINT_POS(std::cout,hop_src);
        // std::cout << " -> ";
        // PRINT_POS(std::cout,hop_dst);
        // std::cout << std::endl;
        // std::cout << ": ";
        // std::cout << std::setw(10) << hop_cnt[hop_src_dst] << " + " << std::setw(10) << size << std::endl;

        hop_cnt[hop_src_dst] += size;
        // from_src[hop_src_dst][root] += size;
        // src_cnt[hop_src_dst]++;

        hop_src.first.first += x_dir;
    }
    hop_dst.first.first = hop_src.first.first;
    while (hop_src.second != pos_dst.second)
    {
        hop_dst.first.second = hop_src.first.second + y_dir;
        std::pair<noc_pos_t, noc_pos_t> hop_src_dst = {hop_src, hop_dst};

        // PRINT_POS(std::cout,hop_src);
        // std::cout << " -> ";
        // PRINT_POS(std::cout,hop_dst);
        // std::cout << std::endl;
        // std::cout << ": ";
        // std::cout << std::setw(10) << hop_cnt[hop_src_dst] << " + " << std::setw(10) << size << std::endl;

        hop_cnt[hop_src_dst] += size;
        // from_src[hop_src_dst][root] += size;
        // src_cnt[hop_src_dst]++;

        hop_src.second += y_dir;
    }
}

void DataFlow::calc_multicast_loop(int src, std::vector<int> &dsts, int size, int root, int flow_idx, int intvl_idx)
{
    assert(dsts.size() != 0);
    std::pair<int, int> flow = {root, flow_idx};
    std::pair<int, std::pair<int, int>> flow_intlv = {root, {flow_idx, intvl_idx}};

    int lv;
    if(src == root)
        lv = 0;
    else
        lv = 1;
    noc_pos_t pos_src = rid2pos(src, 0);
    noc_pos_t pos_final_dst;
    noc_pos_t pos_hop_src;
    noc_pos_t pos_hop_dst;

    std::vector<int> dsts_n;
    std::vector<int> dsts_s;
    std::vector<int> dsts_e;
    std::vector<int> dsts_w;

    std::set<std::pair<noc_pos_t, noc_pos_t>> hops;
    std::map<noc_pos_t, std::set<noc_pos_t>> hop_dsts;
    for(auto dst : dsts)
    {
        pos_final_dst = rid2pos(dst, 0);
        int final_dst_x = pos_final_dst.first.first;
        int final_dst_y = pos_final_dst.first.second;
        pos_hop_src = rid2pos(src, 0);
        pos_hop_dst = rid2pos(src, 1);
        // std::cout << "----";
        // PRINT_POS(std::cout, pos_hop_src);
        // std::cout << "->";
        // PRINT_POS(std::cout, pos_final_dst);
        // std::cout << "----";
        // std::cout << std::endl;
        while(pos_hop_dst != pos_final_dst)
        {
            int &hop_src_z = pos_hop_src.second;
            int &hop_dst_x = pos_hop_dst.first.first;
            int &hop_dst_y = pos_hop_dst.first.second;
            int &hop_dst_z = pos_hop_dst.second;

            if((pos_hop_src.first == pos_hop_dst.first) && (hop_src_z == 0))
            {
                // std::cout << std::setw(15) << "LOCAL->ROUTER ";
                // local to router
            }
            else if((pos_hop_src.first == pos_final_dst.first) && (hop_src_z != 0))
            {
                // std::cout << std::setw(15) << "ROUTER->LOCAL ";
                hop_dst_z--;
            }
            else if (hop_dst_x == final_dst_x)
            {
                if (hop_dst_y > final_dst_y) // NORTH
                {
                    hop_dst_y -= 1;
                    // std::cout << std::setw(15) << "SOUTH->NORTH ";
                }
                else if (hop_dst_y < final_dst_y) // SOUTH
                {
                    hop_dst_y += 1;
                    // std::cout << std::setw(15) << "NORTH->SOUTH ";
                }
            }
            else
            {
                if (hop_dst_x > final_dst_x) // WEST
                {
                    hop_dst_x -= 1;
                    // std::cout << std::setw(15) << "RIGHT->LEFT ";
                }
                else if (hop_dst_x < final_dst_x) // EAST
                {
                    hop_dst_x += 1;
                    // std::cout << std::setw(15) << "LEFT->RIGHT ";
                }
            }
            hop_dsts[pos_hop_src].insert(pos_hop_dst);
            std::pair<noc_pos_t, noc_pos_t> hop = {pos_hop_src, pos_hop_dst};
            // PRINT_POS(std::cout, pos_hop_src);
            // std::cout << "->";
            // PRINT_POS(std::cout, pos_hop_dst);
            // std::cout << std::endl;

            double bw;
            if(!isNoPlink(hop))
                bw = 1;
            else
                bw = 0.5;
            
            int num_hop = get_num_hop(rid2pos(src, 1), pos_hop_dst);
            E_f_s_i[flow_intlv][hop] = 32;

            if(flow_idx == 0)
                eff_E_f_s_i[flow_intlv][hop] = 32;

            tbl_f[hop].insert(flow_intlv);
            
            hops.insert(hop);
            pos_hop_src = pos_hop_dst;
        }
    }
    bool isCT = false;

    if(dsts.size() > 1)
        isCT = true;
    // for(auto h : hop_dsts)
    // {
    //     if(h.second.size() > 1)
    //     {
    //         isCT = true;
    //     }
    // }

    if(isCT)
        mode[flow_intlv] = FLOW_CTRL::CT;
    else
        mode[flow_intlv] = FLOW_CTRL::WH;
    
    for(auto h : hops)
    {
        hop_cnt[h] += size;
    }
}

void DataFlow::calc_multicast_recursive(int src, std::vector<int> &dsts, int size, int root, int flow_idx)
{
    assert(dsts.size() != 0);
    std::pair<int, int> flow = {root, flow_idx};
    noc_pos_t pos_src = rid2pos(src, 0);    // TODO
    noc_pos_t pos_dst;

    std::vector<int> dsts_n;
    std::vector<int> dsts_s;
    std::vector<int> dsts_e;
    std::vector<int> dsts_w;

    std::set<int> n_dir;
    for (auto dst : dsts)
    {
        pos_dst = rid2pos(dst, 1);  // TODO

        // PRINT_POS(pos_dst);

        if (pos_src.first == pos_dst.first)
        {
            if (pos_src.second > pos_dst.second)
            {
                n_dir.insert(1);
                dsts_n.push_back(dst);
            }
            else if (pos_src.second < pos_dst.second)
            {
                n_dir.insert(2);
                dsts_s.push_back(dst);
            }
        }
        else
        {
            if (pos_src.first > pos_dst.first)
            {
                n_dir.insert(3);
                dsts_w.push_back(dst);
            }
            else if (pos_src.first < pos_dst.first)
            {
                n_dir.insert(4);
                dsts_e.push_back(dst);
            }
        }
    }
    
    if (dsts_w.size() > 0)
    {
#ifdef LOG_ALL
        if (depth == 1)
            PRINT_POS(rid2pos(src));
#endif
        // std::cout << " -> West: ";// << std::endl;
        // for(auto &d : dsts_w)
        //     PRINT_POS(std::cout, rid2pos(d));
        // std::cout << std::endl;
        std::pair<noc_pos_t, noc_pos_t> key = {rid2pos(src,1 ), rid2pos(src-6, 1)}; // TODO
        // E_f_s_i[flow].insert(key);
        // src_cnt[key].insert(root);
        if(n_dir.size() > 1)
            is_mcast_link[key] = true;
        else
        {
            if(is_mcast_link[key] != true)
                is_mcast_link[key] = false;
        }          
        calc_unicast(src, src - 6, size, root);
        calc_multicast_recursive(src - 6, dsts_w, size, root, flow_idx);
#ifdef LOG_ALL
        if (depth == 1)
            std::cout << std::endl;
#endif
    }
    if (dsts_e.size() > 0)
    {
#ifdef LOG_ALL
        if (depth == 1)
            PRINT_POS(rid2pos(src));
#endif
        // std::cout << " -> East: ";// << std::endl;
        // for(auto &d : dsts_e)
        //     PRINT_POS(std::cout,rid2pos(d));
        // std::cout << std::endl;
        std::pair<noc_pos_t, noc_pos_t> key = {rid2pos(src, 1), rid2pos(src+6, 1)}; // TODO
        // E_f_s_i[flow].insert(key);
        // src_cnt[key].insert(root);
        if(n_dir.size() > 1)
            is_mcast_link[key] = true;
        else
        {
            if(is_mcast_link[key] != true)
                is_mcast_link[key] = false;
        }
        calc_unicast(src, src + 6, size, root);
        calc_multicast_recursive(src + 6, dsts_e, size, root, flow_idx);
#ifdef LOG_ALL
        if (depth == 1)
            std::cout << std::endl;
#endif
    }
    if (dsts_n.size() > 0)
    {
#ifdef LOG_ALL
        if (depth == 1)
            PRINT_POS(rid2pos(src));
#endif
        // std::cout << " -> North: ";// << std::endl;
        // for(auto &d : dsts_n)
        //     PRINT_POS(std::cout,rid2pos(d));
        // std::cout << std::endl;
        std::pair<noc_pos_t, noc_pos_t> key = {rid2pos(src,1), rid2pos(src+1,1)};   // TODO
        // E_f_s_i[flow].insert(key);
        // src_cnt[key].insert(root);
        if(n_dir.size() > 1)
            is_mcast_link[key] = true;
        else
        {
            if(is_mcast_link[key] != true)
                is_mcast_link[key] = false;
        }
        calc_unicast(src, src + 1, size, root);
        calc_multicast_recursive(src + 1, dsts_n, size, root, flow_idx);
#ifdef LOG_ALL
        if (depth == 1)
            std::cout << std::endl;
#endif
    }
    if (dsts_s.size() > 0)
    {
#ifdef LOG_ALL
        if (depth == 1)
            PRINT_POS(rid2pos(src));
#endif
        // std::cout << " -> South: ";// << std::endl;
        // for(auto &d : dsts_s)
        //     PRINT_POS(std::cout,rid2pos(d));
        // std::cout << std::endl;
        std::pair<noc_pos_t, noc_pos_t> key = {rid2pos(src,1), rid2pos(src-1,1)};   // TODO
        // E_f_s_i[flow].insert(key);
        // src_cnt[key].insert(root);
        if(n_dir.size() > 1)
            is_mcast_link[key] = true;
        else
        {
            if(is_mcast_link[key] != true)
                is_mcast_link[key] = false;
        }
        calc_unicast(src, src - 1, size, root);
        calc_multicast_recursive(src - 1, dsts_s, size, root, flow_idx);
#ifdef LOG_ALL
        if (depth == 1)
            std::cout << std::endl;
#endif
    }
}

unsigned int DataFlow::getIfmSize(int dpathID)
{
    unsigned int sz = 0;
    for (auto op : ops)
    {
        for (auto part : op.ifmPartitions)
        {
            if (dpathID == part.dpathID)
            {
                sz = part.num_row * (part.rowEnd - part.rowStart + 1) * part.num_col * (part.colEnd - part.colStart + 1);
                return sz;
            }
        }
    }
    return 0;
}

unsigned int DataFlow::getWgtSize(int dpathID)
{
    unsigned int sz = 0;
    for (auto op : ops)
    {
        for (auto part : op.wgtPartitions)
        {
            if (dpathID == part.dpathID)
            {
                sz = part.num_row * (part.rowEnd - part.rowStart + 1) * part.num_col * (part.colEnd - part.colStart + 1);
                return sz;
            }
        }
    }
    return 0;
}

unsigned int DataFlow::getOfmSize(int dpathID)
{
    unsigned int sz = 0;
    for (auto op : ops)
    {
        for (auto part : op.ofmPartitions)
        {
            if (dpathID == part.dpathID)
            {
                sz = part.num_row * (part.rowEnd - part.rowStart + 1) * part.num_col * (part.colEnd - part.colStart + 1);
                return sz;
            }
        }
    }
    return 0;
}

DataPartition DataFlow::getOfmPart(int dpathID)
{
    for (auto op : ops)
    {
        for (auto part : op.ofmPartitions)
        {
            if (dpathID == part.dpathID)
            {
                return part;
            }
        }
    }
    DataPartition err;
    return err;
}

OpType DataFlow::getOpType(int dpathID)
{
    for (auto op : ops)
    {
        for (auto part : op.ifmPartitions)
        {
            if (dpathID == part.dpathID)
            {
                return op.type;
            }
        }
    }
    return OpType::ERRTYPE;
}

std::string DataFlow::getOpName(int dpathID)
{
    for (auto &op : ops)
    {
        unsigned int part_no = 0;
        for (auto &part : op.ifmPartitions)
        {
            if (dpathID == part.dpathID)
            {
                if (op.ifmPartitions.size() > 1)
                    return op.name + std::to_string(part_no);
                else
                    return op.name;
            }
            part_no++;
        }
    }
    return "ERR";
}

unsigned int DataFlow::getOpSize(int dpathID)
{
    unsigned int sz = 0;
    for (auto op : ops)
    {
        if (op.type == OpType::GEMM || op.type == OpType::ATTENTION)
        {
            for (auto part : op.ifmPartitions)
            {
                if (dpathID == part.dpathID)
                {
                    sz = part.num_row * (part.rowEnd - part.rowStart + 1) * part.num_col * (part.colEnd - part.colStart + 1); // I * K
                    unsigned int k = 0;
                    for (auto part : op.wgtPartitions)
                    {
                        if (dpathID == part.dpathID)
                            k += part.num_col * (part.colEnd - part.colEnd);
                    }
                    if (k == 0)
                        k = op.wgtDim.cols;

                    sz *= k;
                    return sz;
                }
            }
        }
        else
        {
            for (auto part : op.ifmPartitions)
            {
                if (dpathID == part.dpathID)
                {
                    return getOfmSize(dpathID);
                }
            }
        }
    }
    return 0;
}

_workload DataFlow::getWorkload(int dpathID)
{
    _workload wl;
    wl.h = 0;
    wl.i = 0;
    wl.j = 0;
    wl.k = 0;
    for (auto op : ops)
    {
        if (op.type == OpType::GEMM)
        {
            for (auto part : op.ifmPartitions)
            {
                if (dpathID == part.dpathID)
                {
                    wl.i = (part.rowEnd - part.rowStart + 1);
                    wl.k = (part.colEnd - part.colStart + 1);

                    for (auto wgt_part : op.wgtPartitions)
                    {
                        if (dpathID == wgt_part.dpathID)
                            wl.j += (wgt_part.colEnd - wgt_part.colEnd);
                    }
                    if (wl.j == 0)
                        wl.j = op.wgtDim.cols;

                    return wl;
                }
            }
        }
        else
        {
            for (auto part : op.ifmPartitions)
            {
                if (dpathID == part.dpathID)
                {
                    wl.i = (part.rowEnd - part.rowStart + 1);
                    wl.j = (part.colEnd - part.colStart + 1);
                    wl.k = 0;
                    return wl;
                }
            }
        }
    }
    return wl;
}

unsigned int DataFlow::getPipeDepth()
{
    int depth = 0;
    for (auto &op : ops)
    {
        if (op.pipe_depth > depth)
            depth = op.pipe_depth;
    }
    return depth;
}

void autoGeneratePartitions(Operation &op)
{
    // GEMM 연산의 경우: Y = A * B, where A: inDesc, B: weightDesc, Y: outDesc.
    if (op.type == OpType::GEMM)
    {
        // output partition의 모양에 따라 row partitioning 여부와 column partitioning 여부를 판단
        bool rowPartition = true;
        bool colPartition = true;
        for (const auto &p : op.ofmPartitions)
        {
            // 행 partition: 열 전체가 사용되었는지 확인 (0 ~ out.cols-1)
            if (p.colStart != 0 || p.colEnd != op.ofmDim.cols - 1)
            {
                rowPartition = false;
            }
            // 열 partition: 행 전체가 사용되었는지 확인 (0 ~ out.rows-1)
            if (p.rowStart != 0 || p.rowEnd != op.ofmDim.rows - 1)
            {
                colPartition = false;
            }
        }
        if (rowPartition)
        {
            // 행 partitioning: 각 코어는 output의 row 범위에 해당하는 input을 처리.
            // 가중치는 모든 코어에서 동일하게 전체를 사용.
            for (auto &p : op.ofmPartitions)
            {
                DataPartition inPart;
                inPart.dpathID = p.dpathID;
                inPart.rowStart = p.rowStart;
                inPart.rowEnd = p.rowEnd;
                inPart.colStart = 0;
                inPart.colEnd = op.ifmDim.cols - 1;
                op.ifmPartitions.push_back(inPart);

                DataPartition weightPart;
                weightPart.dpathID = p.dpathID;
                weightPart.rowStart = 0;
                weightPart.rowEnd = op.wgtDim.rows - 1;
                weightPart.colStart = 0;
                weightPart.colEnd = op.wgtDim.cols - 1;
                op.wgtPartitions.push_back(weightPart);
            }
        }
        else if (colPartition)
        {
            // 열 partitioning: 각 코어는 output의 column 범위에 해당하는 가중치를 분할.
            // input은 모든 코어가 전체를 사용.
            for (auto &p : op.ofmPartitions)
            {
                DataPartition inPart;
                inPart.dpathID = p.dpathID;
                inPart.rowStart = 0;
                inPart.rowEnd = op.ifmDim.rows - 1;
                inPart.colStart = 0;
                inPart.colEnd = op.ifmDim.cols - 1;
                op.ifmPartitions.push_back(inPart);

                DataPartition weightPart;
                weightPart.dpathID = p.dpathID;
                weightPart.rowStart = 0;
                weightPart.rowEnd = op.wgtDim.rows - 1;
                weightPart.colStart = p.colStart;
                weightPart.colEnd = p.colEnd;
                op.wgtPartitions.push_back(weightPart);
            }
        }
        else
        {
            // 2D block partitioning: input은 output의 row 범위를, weight는 output의 column 범위를 따름.
            for (auto &p : op.ofmPartitions)
            {
                DataPartition inPart;
                inPart.dpathID = p.dpathID;
                inPart.rowStart = p.rowStart;
                inPart.rowEnd = p.rowEnd;
                inPart.colStart = 0;
                inPart.colEnd = op.ifmDim.cols - 1;
                op.ifmPartitions.push_back(inPart);

                DataPartition weightPart;
                weightPart.dpathID = p.dpathID;
                weightPart.rowStart = 0;
                weightPart.rowEnd = op.wgtDim.rows - 1;
                weightPart.colStart = p.colStart;
                weightPart.colEnd = p.colEnd;
                op.wgtPartitions.push_back(weightPart);
            }
        }
    }
    else if (op.type == OpType::ATTENTION)
    {
        // output partition의 모양에 따라 row partitioning 여부와 column partitioning 여부를 판단
        bool rowPartition = true;
        bool colPartition = true;
        for (const auto &p : op.ofmPartitions)
        {
            // 행 partition: 열 전체가 사용되었는지 확인 (0 ~ out.cols-1)
            if (p.colStart != 0 || p.colEnd != op.ofmDim.cols * op.ofmDim.n_c - 1)
            {
                rowPartition = false;
            }
            // 열 partition: 행 전체가 사용되었는지 확인 (0 ~ out.rows-1)
            if (p.rowStart != 0 || p.rowEnd != op.ofmDim.rows * op.ofmDim.n_r - 1)
            {
                colPartition = false;
            }
        }
        if (rowPartition)
        {
            // 행 partitioning: 각 코어는 output의 row 범위에 해당하는 input을 처리.
            // 가중치는 모든 코어에서 동일하게 전체를 사용.
            for (auto &p : op.ofmPartitions)
            {
                DataPartition inPart;
                inPart.dpathID = p.dpathID;
                inPart.num_row = p.num_row;
                inPart.rowStart = p.rowStart;
                inPart.rowEnd = p.rowEnd;
                inPart.num_col = p.num_col;
                inPart.colStart = 0;
                inPart.colEnd = op.ifmDim.cols - 1;
                op.ifmPartitions.push_back(inPart);

                DataPartition weightPart;
                weightPart.dpathID = p.dpathID;
                weightPart.num_row = p.num_row;
                weightPart.rowStart = 0;
                weightPart.rowEnd = op.wgtDim.rows - 1;
                weightPart.num_col = p.num_col;
                weightPart.colStart = 0;
                weightPart.colEnd = op.wgtDim.cols - 1;
                op.wgtPartitions.push_back(weightPart);
            }
        }
        else if (colPartition)
        {
            // 열 partitioning: 각 코어는 output의 column 범위에 해당하는 가중치를 분할.
            // input은 모든 코어가 전체를 사용.
            for (auto &p : op.ofmPartitions)
            {
                DataPartition inPart;
                inPart.dpathID = p.dpathID;
                inPart.num_row = p.num_row;
                inPart.rowStart = 0;
                inPart.rowEnd = op.ifmDim.rows - 1;
                inPart.num_col = p.num_col;
                inPart.colStart = 0;
                inPart.colEnd = op.ifmDim.cols - 1;
                op.ifmPartitions.push_back(inPart);

                DataPartition weightPart;
                weightPart.dpathID = p.dpathID;
                weightPart.num_row = p.num_row;
                weightPart.rowStart = 0;
                weightPart.rowEnd = op.wgtDim.rows - 1;
                weightPart.num_col = p.num_col;
                weightPart.colStart = p.colStart;
                weightPart.colEnd = p.colEnd;
                op.wgtPartitions.push_back(weightPart);
            }
        }
        else
        {
            // 2D block partitioning: input은 output의 row 범위를, weight는 output의 column 범위를 따름.
            for (auto &p : op.ofmPartitions)
            {
                DataPartition inPart;
                inPart.dpathID = p.dpathID;
                inPart.num_row = p.num_row;
                inPart.rowStart = p.rowStart;
                inPart.rowEnd = p.rowEnd;
                inPart.num_col = p.num_col;
                inPart.colStart = 0;
                inPart.colEnd = op.ifmDim.cols - 1;
                op.ifmPartitions.push_back(inPart);

                DataPartition weightPart;
                weightPart.dpathID = p.dpathID;
                weightPart.num_row = p.num_row;
                weightPart.rowStart = 0;
                weightPart.rowEnd = op.wgtDim.rows - 1;
                weightPart.num_col = p.num_col;
                weightPart.colStart = p.colStart;
                weightPart.colEnd = p.colEnd;
                op.wgtPartitions.push_back(weightPart);
            }
        }
    }
    else if (op.type == OpType::ELEMENT_WISE)
    {
        for (auto &p : op.ofmPartitions)
        {
            DataPartition inPart;
            inPart.dpathID = p.dpathID;
            inPart.num_row = p.num_row;
            inPart.rowStart = p.rowStart;
            inPart.rowEnd = p.rowEnd;
            inPart.num_col = p.num_col;
            inPart.colStart = p.colStart;
            inPart.colEnd = p.colEnd;
            op.ifmPartitions.push_back(inPart);
        }
    }
    else if (op.type == OpType::TRANSPOSE)
    {
        for (auto &p : op.ofmPartitions)
        {
            DataPartition inPart;
            inPart.dpathID = p.dpathID;
            inPart.rowStart = p.colStart;
            inPart.rowEnd = p.colEnd;
            inPart.colStart = p.rowStart;
            inPart.colEnd = p.rowEnd;
            op.ifmPartitions.push_back(inPart);
        }
    }
    else
    {
        // operation not specified
        for (auto &p : op.ofmPartitions)
        {
            DataPartition inPart;
            inPart.dpathID = p.dpathID;
            inPart.rowStart = p.rowStart;
            inPart.rowEnd = p.rowEnd;
            inPart.colStart = p.colStart;
            inPart.colEnd = p.colEnd;
            op.ifmPartitions.push_back(inPart);
        }
    }
}

void DataFlow::calc_NoCTraffic_recursive(const std::vector<DRAMData> &drams, int pass)
{
#ifdef LOG_ALL
    std::cout << "\n=== DRAM-to-Core Communication ===\n";
#endif
    hop_cnt.clear();
    for (const auto &d : drams)
    {
        std::vector<DataSegment> segs;
        for (const auto &seg : d.segments)
        {
            if (seg.dataType == "input" || seg.dataType == "weight")
                segs.push_back(seg);
        }
        if (segs.empty())
            continue;
        for (const auto &seg : segs)
        {
            std::map<std::string, std::vector<int>> group;
            DataPartition dp = seg.partition;

            std::map<std::string, std::vector<int>> dsts;
            std::map<std::string, int> dsts_size;

            for (auto op : ops)
            {
                if (op.name != seg.op_name)
                    continue;

                if (pass > (op.pipe_depth * 2) + 63)
                    continue;

                if (seg.dataType == "input")
                {
                    for (const auto &p : op.ifmPartitions)
                    {
                        int rstart = std::max(dp.rowStart, p.rowStart);
                        int rend = std::min(dp.rowEnd, p.rowEnd);
                        int cstart = std::max(dp.colStart, p.colStart);
                        int cend = std::min(dp.colEnd, p.colEnd);
                        if (rstart <= rend && cstart <= cend)
                        {

                            std::string key = std::to_string(dp.num_row) + "x(" + std::to_string(rstart) + "-" + std::to_string(rend) + ")_" +
                                              std::to_string(dp.num_col) + "x(" + std::to_string(cstart) + "-" + std::to_string(cend) + ")";
                            group[key].push_back(p.dpathID);

                            // Check Hop Count
                            dsts[key].push_back(dpathid2rid(p.dpathID));
                            int data_size = (dp.num_row * (rend - rstart + 1)) * (dp.num_col * (cend - cstart + 1));
                            assert(data_size % 512 == 0);
                            dsts_size[key] = data_size;
                        }
                    }
                }
                else if(seg.dataType == "weight")
                {
                    if (op.type != OpType::ATTENTION && pass != (op.pipe_depth * 2))
                        continue;

                    if (op.type == OpType::ATTENTION && pass < (op.pipe_depth * 2))
                        continue;

                    for (const auto &p : op.wgtPartitions)
                    {
                        int rstart = std::max(dp.rowStart, p.rowStart);
                        int rend = std::min(dp.rowEnd, p.rowEnd);
                        int cstart = std::max(dp.colStart, p.colStart);
                        int cend = std::min(dp.colEnd, p.colEnd);
                        if (rstart <= rend && cstart <= cend)
                        {
                            std::string key = std::to_string(dp.num_row) + "x(" + std::to_string(rstart) + "-" + std::to_string(rend) + ")_" +
                                              std::to_string(dp.num_col) + "x(" + std::to_string(cstart) + "-" + std::to_string(cend) + ")";
                            group[key].push_back(p.dpathID);

                            // Check Hop Count
                            dsts[key].push_back(dpathid2rid(p.dpathID));
                            int data_size = (dp.num_row * (rend - rstart + 1)) * (dp.num_col * (cend - cstart + 1));
                            assert(data_size % 512 == 0);
                            dsts_size[key] = data_size;
                        }
                    }
                }
#ifdef LOG_ALL
                std::cout << "DRAM_" << d.dramID << " (" << seg.dataType << ", " << seg.op_name << ") partition: Rows("
                          << dp.rowStart << "~" << dp.rowEnd << "), Cols(" << dp.colStart << "~" << dp.colEnd << ")\n";

                if (!group.empty())
                {
                    for (auto &g : group)
                    {
                        if (g.second.size() > 1)
                            std::cout << "  -> Multicast to cores: ";
                        else
                            std::cout << "  -> Core " << g.second[0] << " receives : ";
                        std::cout << g.first << "\n";
                    }
                }
                else
                {
                    std::cout << "  -> No matching core partition; broadcast/gather required.\n";
                }
#endif
                // Check Hop Count
                int i = 0;
                for (auto &dst : dsts)
                {
                    // std::cout << "DRAM" << d.dramID << " " << dst.first << " => " << dsts_size[dst.first] / 512 << std::endl;
                    int num_remain_pkt = ceil((double)dsts_size[dst.first] / 512);
                    // std::cout << "num_remain_pkt: " << num_remain_pkt << std::endl;
                    std::map<int, int> num_snd_pkt_ni;
                    int idx_ni = 0;
                    while (num_remain_pkt > 0)
                    {
                        num_snd_pkt_ni[idx_ni]++;
                        idx_ni = (idx_ni + 1) % 3;
                        num_remain_pkt--;
                    }

                    
                    for (int ni = 0; ni < 3; ni++)
                    {
                        // f_s_i[cmpntid2rid(d.dramID) + ni][dst.second] += num_snd_pkt_ni[ni] * 18;
                        // std::cout << "dst:";
                        // for (auto &d : dst.second)
                        //     std::cout << d << std::endl;

                        // std::cout <<  "DRAM" << d.dramID << "num_snd_pkt_ni:" << num_snd_pkt_ni[ni] << std::endl;
                        calc_multicast_recursive(cmpntid2rid(d.dramID) + ni, dst.second, num_snd_pkt_ni[ni] * 18, cmpntid2rid(d.dramID) + ni, i);
#ifdef LOG_ALL
                        std::cout << std::endl;
#endif
                    }
                    i++;
                }
            }
        }
    }
#ifdef LOG_ALL
    std::cout << "\n=== Core-to-Core Communication ===\n";
#endif
    for (const auto &src_op : ops)
    {
        if ((src_op.pipe_depth * 2) + 2 > pass || pass > (src_op.pipe_depth * 2) + 65)
            continue;

        std::vector<const Operation *> dst_ops;
        for (const auto &c : conns)
        {
            if (c.fromDRAM || c.toDRAM)
                continue;
            if (c.srcOp.empty() || c.dstOp.empty())
                continue;

            if (c.srcOp == src_op.name)
            {
                for (const auto &dst_op : ops)
                {
                    if (c.dstOp == dst_op.name)
                        dst_ops.push_back(&dst_op);
                }
            }
        }

        for (const auto &src_ofm : src_op.ofmPartitions)
        {
            bool rowmajor;
            int idx_start;
            int idx_end;
            if(src_ofm.colEnd - src_ofm.colStart + 1 == 512)
            {
                rowmajor = true;
                idx_start = src_ofm.rowStart;
                idx_end = src_ofm.rowEnd;
            }
            else
            {
                rowmajor = false;
                idx_start = src_ofm.colStart;
                idx_end = src_ofm.colEnd;
            }
            std::map<int, std::vector<int>> dsts;
            for (int idx = idx_start; idx <=  idx_end; idx++)
            {
                for (const auto dst_op : dst_ops)
                {
                    for (const auto dst_ifm : dst_op->ifmPartitions)
                    {
                        int istart, iend;
                        if(rowmajor)
                        {
                            istart = std::max(src_ofm.rowStart, dst_ifm.rowStart);
                            iend = std::min(src_ofm.rowEnd, dst_ifm.rowEnd);
                        }
                        else
                        {
                            istart = std::max(src_ofm.colStart, dst_ifm.colStart);
                            iend = std::min(src_ofm.colEnd, dst_ifm.colEnd);
                        }
                        if (istart <= idx && idx <= iend)
                            dsts[idx].push_back(dpathid2rid(dst_ifm.dpathID));
                    }
                }
                if(src_op.ofmDRAM != -1)
                    dsts[idx].push_back(cmpntid2rid(src_op.ofmDRAM) + ((idx - src_ofm.rowStart) % 3));
            }
            
            // int i = 0;
            for (auto dst : dsts)
            {
                // f_s_i[dpathid2rid(src_ofm.dpathID)][dst.second] += 18;
                // std::cout << src_ofm.dpathID << " " << std::setw(3) << i.first << ":";
                // for (auto d : i.second)
                //     std::cout << d << " ";
                // std::cout << std::endl;

                // calc_multicast_recursive(dpathid2rid(src_ofm.dpathID), dst.second, 18, dpathid2rid(src_ofm.dpathID), f_s_i[dpathid2rid(src_ofm.dpathID)].size() - 1);
                // i++;
            }
            
        }

        
        
        dst_ops.clear();
    }

    // TODO
    // for(auto hop : hop_cnt)
    // {

    //     int src_x = hop.first.first.first;
    //     int src_y = hop.first.first.second;
    //     int dst_x = hop.first.second.first;
    //     int dst_y = hop.first.second.second;
    //     if(src_x == -1 && dst_x == 0)
    //         hop_cnt[hop.first] *= 2;
    //     else if(src_x == 0 && dst_x == -1)
    //         hop_cnt[hop.first] *= 2;
    //     else if(src_x == 5 && dst_x == 6)
    //         hop_cnt[hop.first] *= 2;
    //     else if(src_x == 6 && dst_x == 5)
    //         hop_cnt[hop.first] *= 2;
    //     else if(src_y == 2 && dst_y == 3)
    //         hop_cnt[hop.first] *= 2;
    //     else if(src_y == 3 && dst_y == 2)
    //         hop_cnt[hop.first] *= 2;
        
        
    // }
}


void DataFlow::calc_NoCTraffic_loop(const std::vector<DRAMData> &drams, int pass)
{
#ifdef LOG_ALL
    std::cout << "\n=== DRAM-to-Core Communication ===\n";
#endif
    hop_cnt.clear();
    tbl_f.clear();
    tbl_bw.clear();
    tbl_ct.clear();    
    eff_tbl_ct.clear();
    tbl_wh.clear();
    eff_tbl_wh.clear();
    f_s_i.clear();
    E_f_s_i.clear();
    eff_E_f_s_i.clear();

    for (const auto &d : drams)
    {
        std::vector<DataSegment> segs;
        for (const auto &seg : d.segments)
        {
            if (seg.dataType == "input" || seg.dataType == "weight")
                segs.push_back(seg);
        }
        if (segs.empty())
            continue;

        int i = 0;
        for (const auto &seg : segs)
        {
            std::map<std::string, std::vector<int>> group;
            DataPartition dp = seg.partition;

            std::map<std::string, std::vector<int>> dsts;
            std::map<std::string, int> dsts_size;

            for (auto op : ops)
            {
                
                if (op.name != seg.op_name)
                    continue;

                if (pass > (op.pipe_depth * 2) + 63)
                    continue;

                if (seg.dataType == "input")
                {
                    for (const auto &p : op.ifmPartitions)
                    {
                        int rstart = std::max(dp.rowStart, p.rowStart);
                        int rend = std::min(dp.rowEnd, p.rowEnd);
                        int cstart = std::max(dp.colStart, p.colStart);
                        int cend = std::min(dp.colEnd, p.colEnd);
                        if (rstart <= rend && cstart <= cend)
                        {

                            std::string key = std::to_string(dp.num_row) + "x(" + std::to_string(rstart) + "-" + std::to_string(rend) + ")_" +
                                              std::to_string(dp.num_col) + "x(" + std::to_string(cstart) + "-" + std::to_string(cend) + ")";
                            group[key].push_back(p.dpathID);

                            // Check Hop Count
                            dsts[key].push_back(dpathid2rid(p.dpathID));
                            int data_size = (dp.num_row * (rend - rstart + 1)) * (dp.num_col * (cend - cstart + 1));
                            assert(data_size % 512 == 0);
                            dsts_size[key] = data_size;
                        }
                    }
                }
                else if(seg.dataType == "weight")
                {
                    if (op.type != OpType::ATTENTION && pass != (op.pipe_depth * 2))
                        continue;

                    if (op.type == OpType::ATTENTION && pass < (op.pipe_depth * 2))
                        continue;

                    for (const auto &p : op.wgtPartitions)
                    {
                        int rstart = std::max(dp.rowStart, p.rowStart);
                        int rend = std::min(dp.rowEnd, p.rowEnd);
                        int cstart = std::max(dp.colStart, p.colStart);
                        int cend = std::min(dp.colEnd, p.colEnd);
                        if (rstart <= rend && cstart <= cend)
                        {
                            std::string key = std::to_string(dp.num_row) + "x(" + std::to_string(rstart) + "-" + std::to_string(rend) + ")_" +
                                              std::to_string(dp.num_col) + "x(" + std::to_string(cstart) + "-" + std::to_string(cend) + ")";
                            group[key].push_back(p.dpathID);

                            // Check Hop Count
                            dsts[key].push_back(dpathid2rid(p.dpathID));
                            int data_size = (dp.num_row * (rend - rstart + 1)) * (dp.num_col * (cend - cstart + 1));
                            assert(data_size % 512 == 0);
                            dsts_size[key] = data_size;
                        }
                    }
                }
#ifdef LOG_ALL
                std::cout << "DRAM_" << d.dramID << " (" << seg.dataType << ", " << seg.op_name << ") partition: Rows("
                          << dp.rowStart << "~" << dp.rowEnd << "), Cols(" << dp.colStart << "~" << dp.colEnd << ")\n";

                if (!group.empty())
                {
                    for (auto &g : group)
                    {
                        if (g.second.size() > 1)
                            std::cout << "  -> Multicast to cores: ";
                        else
                            std::cout << "  -> Core " << g.second[0] << " receives : ";
                        std::cout << g.first << "\n";
                    }
                }
                else
                {
                    std::cout << "  -> No matching core partition; broadcast/gather required.\n";
                }
#endif
                // Check Hop Count
                int ii = 0;
                int s_idx = cmpntid2rid(d.dramID);
                for (auto &dst : dsts)
                {
                    // std::cout << "DRAM" << d.dramID << " " << dst.first << " => " << dsts_size[dst.first] / 512 << std::endl;
                    int num_remain_pkt = ceil((double)dsts_size[dst.first] / 512);
                    // std::cout << "num_remain_pkt: " << num_remain_pkt << std::endl;
                    std::map<int, int> num_snd_pkt_ni;
                    int idx_ni = 0;
                    while (num_remain_pkt > 0)
                    {
                        num_snd_pkt_ni[idx_ni]++;
                        idx_ni = (idx_ni + 1) % 3;
                        num_remain_pkt--;
                    }

                    
                    for (int ni = 0; ni < 3; ni++)
                    {
                        flow_idx_t s_i = {s_idx + ni, {i, ii}};
                        f_s_i[s_i] = {dst.second, num_snd_pkt_ni[ni] * 18 * 32};
                        // std::cout << "dst:";
                        // for (auto &d : dst.second)
                        //     std::cout << d << std::endl;

                        // std::cout <<  "DRAM" << d.dramID << "num_snd_pkt_ni:" << num_snd_pkt_ni[ni] << std::endl;
                        calc_multicast_loop(s_idx + ni, dst.second, num_snd_pkt_ni[ni] * 18, s_idx + ni, i, ii);
#ifdef LOG_ALL
                        std::cout << std::endl;
#endif
                    }
                    ii++;
                }
                
            }
            i++;
        }
    }
#ifdef LOG_ALL
    std::cout << "\n=== Core-to-Core Communication ===\n";
#endif
    for (const auto &src_op : ops)
    {
        if ((src_op.pipe_depth * 2) + 2 > pass || pass > (src_op.pipe_depth * 2) + 65)
            continue;

        std::vector<const Operation *> dst_ops;
        for (const auto &c : conns)
        {
            if (c.fromDRAM || c.toDRAM)
                continue;
            if (c.srcOp.empty() || c.dstOp.empty())
                continue;

            if (c.srcOp == src_op.name)
            {
                for (const auto &dst_op : ops)
                {
                    if (c.dstOp == dst_op.name)
                        dst_ops.push_back(&dst_op);
                }
            }
        }

        for (const auto &src_ofm : src_op.ofmPartitions)
        {
            bool rowmajor;
            int idx_start;
            int idx_end;
            if(src_ofm.colEnd - src_ofm.colStart + 1 == 512)
            {
                rowmajor = true;
                idx_start = src_ofm.rowStart;
                idx_end = src_ofm.rowEnd;
            }
            else
            {
                rowmajor = false;
                idx_start = src_ofm.colStart;
                idx_end = src_ofm.colEnd;
            }
            std::map<int, std::vector<int>> dsts;
            for (int idx = idx_start; idx <=  idx_end; idx++)
            {
                for (const auto dst_op : dst_ops)
                {
                    for (const auto dst_ifm : dst_op->ifmPartitions)
                    {
                        int istart, iend;
                        if(rowmajor)
                        {
                            istart = std::max(src_ofm.rowStart, dst_ifm.rowStart);
                            iend = std::min(src_ofm.rowEnd, dst_ifm.rowEnd);
                        }
                        else
                        {
                            istart = std::max(src_ofm.colStart, dst_ifm.colStart);
                            iend = std::min(src_ofm.colEnd, dst_ifm.colEnd);
                        }
                        if (istart <= idx && idx <= iend)
                            dsts[idx].push_back(dpathid2rid(dst_ifm.dpathID));
                    }
                }
                if(src_op.ofmDRAM != -1)
                    dsts[idx].push_back(cmpntid2rid(src_op.ofmDRAM) + ((idx - src_ofm.rowStart) % 3));
            }

            
            int s_idx = dpathid2rid(src_ofm.dpathID);
            int cnt_dst = 0;

            std::map<std::vector<int>, int> dst_idx;
            for (auto dst : dsts)
            {
                int flow_idx;
                int intvl_idx;

                if(dst_idx.find(dst.second) == dst_idx.end())
                {
                    dst_idx[dst.second] = cnt_dst;
                    cnt_dst++;
                }

                if(src_op.ofmDRAM != -1)
                {
                    flow_idx = dst_idx[dst.second] / 3;
                    intvl_idx = dst_idx[dst.second] % 3;
                }
                else
                {
                    flow_idx = dst_idx[dst.second];
                    intvl_idx = 0;
                }
                flow_idx_t s_i = {s_idx, {flow_idx, intvl_idx}};
                if(f_s_i[s_i].first.empty())
                    f_s_i[s_i].first = dst.second;
                
                f_s_i[s_i].second += 18 * 32;
                calc_multicast_loop(dpathid2rid(src_ofm.dpathID), dst.second, 18, dpathid2rid(src_ofm.dpathID), flow_idx, intvl_idx);
            }
        }

        dst_ops.clear();
    }
#ifdef LOG_ALL
    for(auto &f : f_s_i)
    {
        PRINT_FLOW(std::cout, f.first);
        std::cout << "D={";
        for(auto &d : f.second.first)
        {
            std::cout << d << ",";
        }
        std::cout << "}, " << f.second.second;
        std::cout << std::endl;
    }
#endif
    for(auto hop : hop_cnt)
    {
        // TODO
        // int src_x = hop.first.first.first;
        // int src_y = hop.first.first.second;
        // int dst_x = hop.first.second.first;
        // int dst_y = hop.first.second.second;
        // bool isNoP = false;
        // if(src_x == -1 && dst_x == 0)
        //     isNoP = true;
        // else if(src_x == 0 && dst_x == -1)
        //     isNoP = true;
        // else if(src_x == 5 && dst_x == 6)
        //     isNoP = true;
        // else if(src_x == 6 && dst_x == 5)
        //     isNoP = true;
        // else if(src_y == 2 && dst_y == 3)
        //     isNoP = true;
        // else if(src_y == 3 && dst_y == 2)
        //     isNoP = true;

        if (isNoPlink(hop.first))
        {
            hop_cnt[hop.first] *= 2;
            tbl_bw[hop.first] = 16;
        }
        else
        {
            tbl_bw[hop.first] = 32;
            int src_x = hop.first.first.first.first;
            int src_y = hop.first.first.first.second;
            int dst_x = hop.first.second.first.first;
            int dst_y = hop.first.second.first.second;
            if(dst_x == -1 || dst_x == 6)
            {
                if(src_x == dst_x && src_y == dst_y)
                    tbl_bw[hop.first] = 32/3;
            }
        }
    }

    

    for (auto f : E_f_s_i)
    {
        int flow_idx = f.first.second.first;
        if (mode[f.first] == FLOW_CTRL::CT)
        {
            for (auto l : f.second)
            {
                tbl_ct[l.first].insert(f.first);
                if (flow_idx == 0)
                    eff_tbl_ct[l.first].insert(f.first);
            }
        }
        else
        {
            for (auto l : f.second)
            {
                tbl_wh[l.first].insert(f.first);
                if (flow_idx == 0)
                    eff_tbl_wh[l.first].insert(f.first);
            }
            f_wh_only.insert(f.first);
        }
    }


}

void DataFlow::calc_CompDly(int pass)
{
    for (auto &op : ops)
    {
        if ((op.pipe_depth * 2) + 1 > pass || pass > (op.pipe_depth * 2) + 64)
            continue;

        DataPartition ifm = op.ifmPartitions[0];
        // DataPartition wgt = op.wgtPartitions[0];
        DataPartition ofm = op.ofmPartitions[0];

        int n_head;
        int dim_i = (ifm.rowEnd - ifm.rowStart + 1);
        int dim_j = (ofm.colEnd - ofm.colStart + 1);
        int dim_k = (ifm.colEnd - ifm.colStart + 1);

        if (ifm.num_row > 1)
            n_head = ifm.num_row;
        else if (ifm.num_col > 1)
            n_head = ifm.num_col;
        else if (ofm.num_col > 1)
            n_head = ofm.num_col;
        else
            n_head = 1;
#ifdef LOG_ALL
        std::cout << op.name << " / ";
        std::cout << "h: " << n_head << " / i: " << dim_i << " / j:" << dim_j << " / k: " << dim_k;
#endif
        unsigned long long op_comp_dly = 0;
        if (op.type == OpType::GEMM || op.type == OpType::ATTENTION)
        {
            op_comp_dly = (dim_i * dim_j * dim_k * n_head) / 1024;
        }
        else
        {
            op_comp_dly = (dim_i * dim_j * n_head) / 64;
        }
        if (op_comp_dly > max_comp_dly)
            max_comp_dly = op_comp_dly;
#ifdef LOG_ALL
        std::cout << " / comp_dly: " << op_comp_dly << std::endl;
#endif
    }
}

int DataFlow::getDRAMID(const std::vector<DRAMData> &drams, const std::string &op_name, const std::string &dtype)
{
    for (auto &dram : drams)
    {
        for (auto &seg : dram.segments)
        {
            if (seg.op_name == op_name && seg.dataType == dtype)
            {
                return dram.dramID;
            }
        }
    }
    return -1;
}

unsigned int getPartSize(part_t part)
{
    return (part[1] - part[0] + 1) * (part[3] - part[2] + 1);
}

void DataFlow::print()
{
    std::cout << "=== DataFlow Overview ===\n";
    for (auto &c : conns)
    {
        if (c.fromDRAM && !c.dstOp.empty())
            std::cout << "[DRAM_" << c.dramID << "] --(" << c.note << ")--> " << c.dstOp << "\n";
        else if (c.toDRAM && !c.srcOp.empty())
            std::cout << c.srcOp << " --(" << c.note << ")--> [DRAM_" << c.dramID << "]\n";
        else
            std::cout << c.srcOp << " --(" << c.note << ")--> " << c.dstOp << "\n";
    }
    std::cout << "\n=== Operation Details ===\n";
    for (auto &op : ops)
    {
        std::cout << "\n[Operation] " << op.name << "\n";
        std::cout << "  Type: ";
        switch (op.type)
        {
        case OpType::GEMM:
            std::cout << "GEMM";
            break;
        case OpType::ELEMENT_WISE:
            std::cout << "ELEMENT_WISE";
            break;
        case OpType::TRANSPOSE:
            std::cout << "TRANSPOSE";
            break;
        case OpType::MERGE:
            std::cout << "MERGE";
            break;
        case OpType::ATTENTION:
            std::cout << "ATTENTION";
            break;
        default:
            assert("Illegal Operation Type");
            // SC_REPORT_FATAL("MAPPER", "Illegal Operation Type");
        }
        std::cout << "\n  Has weight? " << (op.hasWeight ? "Yes" : "No") << "\n";
        std::cout << "  Input Dim: (" << op.ifmDim.n_r << "x" << op.ifmDim.rows << ")x(" << op.ifmDim.n_c << "x" << op.ifmDim.cols << ")\n";
        std::cout << "  Weight Dim: ";
        if (op.hasWeight)
            std::cout << "(" << op.wgtDim.n_r << "x" << op.wgtDim.rows << ")x(" << op.wgtDim.n_c << "x" << op.wgtDim.cols << ")\n";
        else
            std::cout << "N/A" << "\n";

        std::cout << "  Output Dim: (" << op.ofmDim.n_r << "x" << op.ofmDim.rows << ")x(" << op.ofmDim.n_c << "x" << op.ofmDim.cols << ")\n";
        std::cout << "  Activation DRAM: " << (op.ifmDRAM >= 0 ? std::to_string(op.ifmDRAM) : "N/A") << "\n";
        std::cout << "  Weight DRAM: " << (op.wgtDRAM >= 0 ? std::to_string(op.wgtDRAM) : "N/A") << "\n";
        std::cout << "  Output DRAM: " << (op.ofmDRAM >= 0 ? std::to_string(op.ofmDRAM) : "N/A") << "\n";
        std::cout << "  Partitions:\n";
        for (auto &p : op.ofmPartitions)
        {
            std::cout << "    Core " << p.dpathID << " => " << p.num_row << " x Rows(" << p.rowStart << "~" << p.rowEnd << "), "
                      << p.num_col << " x Cols(" << p.colStart << "~" << p.colEnd << ")\n";
        }
        if (!op.inputOps.empty())
        {
            std::cout << "  InputOps: ";
            for (auto &inp : op.inputOps)
                std::cout << inp << " ";
            std::cout << "\n";
        }
    }
}

bool DataFlow::isNoPlink(link_t link)
{
    int src_x = link.first.first.first;
    int src_y = link.first.first.second;
    int dst_x = link.second.first.first;
    int dst_y = link.second.first.second;
    bool isNoP = false;
    if(src_x == -1 && dst_x == 0)
        isNoP = true;
    else if(src_x == 0 && dst_x == -1)
        isNoP = true;
    else if(src_x == 5 && dst_x == 6)
        isNoP = true;
    else if(src_x == 6 && dst_x == 5)
        isNoP = true;
    else if(src_y == 2 && dst_y == 3)
        isNoP = true;
    else if(src_y == 3 && dst_y == 2)
        isNoP = true;

    return isNoP;
}

NOC_DIR get_dir(link_t link)
{
    noc_pos_t src = link.first;
    int src_x = src.first.first;
    int src_y = src.first.second;

    noc_pos_t dst = link.second;
    int dst_x = dst.first.first;
    int dst_y = dst.first.second;

    NOC_DIR dir;

    if(src_x == dst_x && src_y == dst_y)
        dir = NOC_DIR::L;
    else if (src_y > dst_y)
        dir = NOC_DIR::N;
    else if (src_y < dst_y)
        dir = NOC_DIR::S;
    else if (src_x > dst_x)
        dir = NOC_DIR::W;
    else
        dir = NOC_DIR::E;
    
    return dir;
}

int get_num_hop(noc_pos_t src, noc_pos_t dst)
{
    int src_x = src.first.first;
    int src_y = src.first.second;
    int dst_x = dst.first.first;
    int dst_y = dst.first.second;
    int dst_z = dst.second;

    int num_hop;
    if(src_x > dst_x)
        num_hop = src_x - dst_x;
    else
        num_hop = dst_x - src_x;
    
    if(src_y > dst_y)
        num_hop += src_y - dst_y;
    else
        num_hop += dst_y - src_y;
    
    if(dst_z == 0)
        num_hop++;

    
    return num_hop;
}