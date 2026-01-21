#include "spatial_mapping/light_placement.h"
#include "cluster.h"
#include "schnode.h"
#include "layerengine.h"
#include <cassert>
#include <random>
#include "util.h"
#include "log.h"



FastLayerEngine *Light_placement::fast_buffer = nullptr;

Light_partition::Light_partition()
{
}

void Light_partition::init(const fmap_shape &shape, len_t _batch, vol_t _layerno)
{
    PartSch partSch;
    batch = _batch;
    auto partIter = partEngine.init(siz, batch, network->getNode(_layerno), partSch, 0);
    allpartition.clear();
    do
    {
        if (partSch.B <= batch && partSch.K <= shape.c && partSch.H <= shape.h && partSch.W <= shape.w)
        {
            std::vector<len_t> fac = {partSch.B, partSch.K, partSch.H, partSch.W};
            allpartition.push_back(fac);
        }
    } while (partIter.nextPart());
    assert(!allpartition.empty());
}

void Light_partition::re_init(const fmap_shape &shape, vol_t size, PartSch part)
{
    PartSch partSch;
    siz = size;
    b = part.B;
    c = part.K;
    h = part.H;
    w = part.W;
    auto partIter = partEngine.init(siz, batch, network->getNode(layerno), partSch, 0);
    allpartition.clear();
    do
    {
        if (partSch.B <= batch && partSch.K <= shape.c && partSch.H <= shape.h && partSch.W <= shape.w)
        {
            std::vector<len_t> fac = {partSch.B, partSch.K, partSch.H, partSch.W};
            allpartition.push_back(fac);
        }
    } while (partIter.nextPart());
    assert(!allpartition.empty());
}

Light_partition::Light_partition(const fmap_shape &shape, cidx_t _siz, len_t _batch, vol_t _layerno)
{
    PartSch partSch;
    siz = _siz;
    layerno = _layerno;
    batch = _batch;
    auto partIter = partEngine.init(siz, batch, network->getNode(_layerno), partSch, 0);
    do
    {
        if (partSch.B <= batch && partSch.K <= shape.c && partSch.H <= shape.h && partSch.W <= shape.w)
        {
            if (_layerno == 21 || _layerno == 20)
            {
                if (partSch.K == 1)
                {
                    std::vector<len_t> fac = {partSch.B, partSch.K, partSch.H, partSch.W};
                    allpartition.push_back(fac);
                }
            }
            else
            {
                // int num1 = 0;
                // if(partSch.B == 1)
                //     num1++;
                // if(partSch.K == 1)
                //     num1++;
                // if(partSch.H == 1)
                //     num1++;
                // if(partSch.W == 1)
                //     num1++;
                // if(num1 >= 3)
                // {
                //     std::vector<len_t> fac={partSch.B, partSch.K, partSch.H, partSch.W};
                //     allpartition.push_back(fac);
                // }
                std::vector<len_t> fac = {partSch.B, partSch.K, partSch.H, partSch.W};
                allpartition.push_back(fac);
            }
        }
    } while (partIter.nextPart());

    if (!allpartition.empty())
        change();
}

Light_partition::Light_partition(vol_t _b, vol_t _c, vol_t _h, vol_t _w, vol_t _siz, vol_t _layerno) : b(_b), c(_c), h(_h), w(_w), siz(_siz), layerno(_layerno)
{
}

Light_placement::Layer_partition::Layer_partition()
{
}

Light_placement::Layer_partition::Layer_partition(lid_t _layerno, cidx_t _partno) : layerno(_layerno), partno(_partno) {}

Light_placement::Light_placement() {}

/*Light_placement::Light_placement(SchNode* segment){
    assert(segment->get_type()==SchNode::NodeType::S);
    auto pt = dynamic_cast<SCut*>(segment);
}*/

Light_placement::Light_placement(LTreeNode *_node)
{
    node = _node;
    const auto &cnodes = node->get_children();
    layer_num = 0;
    cidx_t cnum = static_cast<cidx_t>(cnodes.size());
    assert(cnum > 0);
    utime_t *tlist = new utime_t[cnum];
    utime_t *cur_item = tlist;
    for (auto child : cnodes)
    {
        *(cur_item++) = child->get_utime();
    }
    Cluster cluster(0, Cluster::xlen * Cluster::ylen);
    auto allocRes = cluster.try_alloc(tlist, cnum);
    delete[] tlist;

    int allocidx = 0;
    layers = node->layers();
    FOR_BITSET(layerno, node->layers())
    {
        for (int j = 0; j < allocRes[allocidx + 1] - allocRes[allocidx]; ++j)
        {
            placement.emplace_back(layerno, j);
            layer_scheme[layerno].insert(allocRes[allocidx] + j);
        }
        partition.emplace_back(network->getNode(layerno).layer().ofmap_shape(), allocRes[allocidx + 1] - allocRes[allocidx], node->get_bgrp_size(), layerno);
        ++allocidx;
        ++layer_num;
    }
}

const std::vector<Light_partition> &Light_placement::get_partition() const
{
    return partition;
}

const std::unordered_map<lid_t, std::unordered_set<cidx_t>> &Light_placement::get_layer_scheme() const
{
    return layer_scheme;
}
const std::unordered_map<lid_t, FetchSch> &Light_placement::get_fetch_scheme() const
{
    return fetch_scheme;
}

const std::vector<Light_placement::Layer_partition> &Light_placement::get_placement() const
{
    return placement;
}

bool Light_placement::swap(cidx_t x, cidx_t y)
{
    bool isSameLayer = false;
    // LOG2("SWAP" << x << " <-> " << y);
    if (placement[x].layerno != placement[y].layerno)
    {
        std::cout << "OP3: " << x << "<->" << y << std::endl;
        layer_scheme[placement[x].layerno].erase(x);
        layer_scheme[placement[x].layerno].insert(y);
        layer_scheme[placement[y].layerno].erase(y);
        layer_scheme[placement[y].layerno].insert(x);
    }
    else
    {
        isSameLayer = true;
        std::cout << "OP2" << x << "<->" << y << std::endl;
    }

    std::swap(placement[x], placement[y]);

    return isSameLayer;
}

void Light_partition::change()
{
    static std::mt19937_64 gen(233);
    int ch = gen() % allpartition.size();
    vol_t temp_b = b;
    vol_t temp_c = c;
    vol_t temp_h = h;
    vol_t temp_w = w;
    if (layerno == 20 || layerno == 21)
    {
        while (allpartition[ch][1] != 1)
        {
            ch = gen() % allpartition.size();
        }
    }
    // else
    // {
    //     int num1 = 0;
    //     do
    //     {
    //         num1 = 0;
    //         for(int i = 0; i < 4; i++)
    //         {
    //             if(allpartition[ch][i] == 1)
    //                 ++num1;
    //         }
    //         ch = gen() % allpartition.size();
    //     } while(num1 <= 2);
    // }
    b = allpartition[ch][0];
    c = allpartition[ch][1];
    h = allpartition[ch][2];
    w = allpartition[ch][3];
}
void Light_placement::change_core(int cnt)
{
    if (cnt == 16)
    {
        return; // exit
    }
    srand(time(NULL));
    static std::mt19937_64 gen(rand());
    cidx_t layer1, layer2, layer1_size, layer2_size;
    cidx_t temp_layer1, temp_layer2;
    int cnt1 = 0;
    do
    {
        temp_layer1 = gen() % layer_num;
        temp_layer2 = gen() % layer_num;
        layer1 = partition[temp_layer1].layerno;
        layer2 = partition[temp_layer2].layerno;
        cnt1++;
        if (cnt1 == 16)
        {
            mutate(Cluster::xlen * Cluster::ylen, 0, 0, 0, 0);
            return;
        }
    } while (layer1 == layer2 || layer_scheme[layer1].size() == 1);

    // layer1_size = layer_scheme[layer1].size();//core number
    // layer2_size = layer_scheme[layer2].size();
    // std::unordered_set<cidx_t>::iterator iter1 = layer_scheme[layer1].begin();
    // cidx_t temp1 = *iter1;
    // cidx_t rand_core_id = gen() % layer1_size;
    // for (cidx_t i = 0; i < rand_core_id; ++i) {
    //     temp1 = *(++iter1);
    // }
    // layer1_size--;
    // layer2_size++;

    // Cluster c1(0, layer1_size);
    // Cluster c2(0, layer2_size);
    // PartSch part1 = fast_buffer->fast_get_part(c1, network->getNode(layer1), partition[temp_layer1].batch);
    // if (part1.size() == 0) {
    //     change_core(++cnt);
    //     return;
    // }
    // PartSch part2 = fast_buffer->fast_get_part(c2, network->getNode(layer2), partition[temp_layer2].batch);
    // if (part2.size() == 0) {
    //     change_core(++cnt);
    //     return;
    // }
    // layer_scheme[layer1].erase(temp1);
    // placement[temp1].layerno = layer2;
    // partition[temp_layer1].re_init(network->getNode(layer1).layer().ofmap_shape(), layer1_size, part1);
    // partition[temp_layer2].re_init(network->getNode(layer2).layer().ofmap_shape(), layer2_size, part2);
    // for (auto core_id : layer_scheme[layer1]) {
    //     if (placement[core_id].partno > placement[temp1].partno) {
    //         placement[core_id].partno--;
    //     }
    // }
    // cidx_t partnp_temp = gen() % layer2_size;
    // for (auto core_id : layer_scheme[layer2]) {
    //     if (placement[core_id].partno >= partnp_temp) {
    //         placement[core_id].partno++;
    //     }
    // }
    // layer_scheme[layer2].insert(temp1);
    // placement[temp1].partno = partnp_temp;
}

void Light_placement::change_DRAM()
{
    // srand(time(NULL));
    static std::mt19937_64 gen(rand_seed);
    cidx_t layer1;
    cidx_t temp_layer1;
    mlen_t new_ddr = -1;
    int data = -1;
    do
    {
        temp_layer1 = gen() % layer_num;
        layer1 = partition[temp_layer1].layerno;
        data = gen() % 3;
        // data = gen() % 2;
        // new_ddr = gen() % (NoC::DRAM_num+1);  //
        new_ddr = gen() % NoC::DRAM_num;
        if (new_ddr == NoC::DRAM_num)
        {
            new_ddr = -2; // -2: all dram
        }
    } while (new_ddr == layer_DRAM[layer1][data] || layer_DRAM[layer1][data] == -1);
    layer_DRAM[layer1][data] = new_ddr;
}

int Light_placement::mutate(cidx_t dist, int min_idx, int max_idx, int delta, int last_op)
{
    static std::mt19937_64 gen(rand_seed);
    int type = gen() % 11;
    
    int op;
    
    // if(type < 3)
    // {
    //     op = 6;
    // }
    // else if (type < 6)
    if (type < 6)
    {
        op = 3;
    }
    else if(type < 7)
    {
        op = 2;
    }
    else if(type < 8)
    {
        // change_DRAM();
        op = 5;
    }
    else if(type < 9)
    {
        op = 7;
    }
    else if(type < 10)
    {
        op = 8;
    }
    else
    {
        op = 9;
    }

    switch (op)
    {
    case 2:
    {
        std::cout << "OP2" << std::endl;
        cidx_t x, y;
        do
        {
            x = gen() % placement.size();
            y = gen() % placement.size();
        } while ((dis(Cluster::get_pos(x), Cluster::get_pos(y)) > dist) || (placement[x].layerno != placement[y].layerno));
        swap(x, y);
        break;
    }
    case 3:
    {
        std::cout << "OP3" << std::endl;
        cidx_t x, y;
        do
        {
            x = gen() % placement.size();
            y = gen() % placement.size();
        } while ((dis(Cluster::get_pos(x), Cluster::get_pos(y)) > dist) || (placement[x].layerno == placement[y].layerno));
        // } while (placement[x].layerno != 19 && placement[y].layerno != 19);
        swap(x, y);
        break;
    }
    case 5:
        change_DRAM();
        break;
    case 6:
        swap(min_idx, max_idx);
        break;
    case 7:
        std::cout << "OP7" << std::endl;
        change_OA_DRAM();
        break;
    case 8:
        std::cout << "OP8" << std::endl;
        if(!change_MK_row())
            mutate(dist, min_idx, max_idx, delta, last_op);
        break;
    case 9:
        std::cout << "OP9" << std::endl;
        if(!spread_WE())
            mutate(dist, min_idx, max_idx, delta, last_op);
        break;
    default:
        break;
    }
    return op;
}


void Light_placement::crossover(const Light_placement &other)
{
    static std::mt19937_64 gen(277);
    if (gen() & 1)
    {
        placement = other.get_placement();
    }
    for (int i = 0; i < partition.size(); ++i)
    {
        if (gen() & 1)
        {
            partition[i] = other.partition[i];
        }
    }
}

void Light_placement::change_OA_DRAM()
{
    std::unordered_map<lid_t, std::vector<cidx_t>> place;
    cidx_t core_id = 0;
    for (auto &l : placement)
    {
        place[l.layerno].push_back(core_id);
        core_id++;
    }

    std::map<double, std::vector<std::pair<int, int>>> dist_dram;
    for(auto &l : place)
    {
        if(l.first < 3 || l.first == 19)
        {
            Cluster tmp_c(l.second);
            int d = tmp_c.nearest_dram();
            for(auto dist : tmp_c.get_dist_dram())
            {
                for(auto dr : dist.second)
                    dist_dram[dist.first].push_back({dr, l.first});
            }
            layer_DRAM[l.first][2] = 4;
        }
    }

    std::set<int> assigned;
    for(auto dist : dist_dram)
    {
        for(auto dis : dist.second)
        {
            int l = dis.second;
            int d = dis.first;
            // std::cout << dist.first << " | " << l << " | " << d << std::endl;
            if(layer_DRAM[l][2] == 4 && (assigned.find(d) == assigned.end()))
            {
                layer_DRAM[l][2] = d;
                int l2;
                int d2;
                if(l == 0)
                    l2 = 19;
                else if (l == 1)
                    l2 = 2;
                else if (l == 2)
                    l2 = 1;                   
                else if (l == 19)
                    l2 = 0;
                
                if (d == 0)
                    d2 = 1;
                else if (d == 1)
                    d2 = 0;
                else if (d == 2)
                    d2 = 3;
                else if (d == 3)
                    d2 = 2;

                layer_DRAM[l2][2] = d2;
                assigned.insert(d);
                assigned.insert(d2);
            }
        }
        if(assigned.size() == 4)
            break;
    }
}

bool Light_placement::change_MK_row()
{
    std::cout << "OP8" << std::endl;
    static std::mt19937_64 gen(rand_seed);
    std::unordered_map<lid_t, std::vector<cidx_t>> place;
    cidx_t core_id = 0;
    for (auto &l : placement)
    {
        place[l.layerno].push_back(core_id);
        core_id++;
    }
    int cid_MK = place[19].front();
    int y_MK = cid_MK % 6;
    int x_MK = cid_MK / 6;
    int dram_MK = layer_DRAM[19][2];
    int x_dir_MK;
    if(dram_MK < 2)
    {
        x_dir_MK = 0;   // left
    }
    else
    {
        x_dir_MK = 1;   // right
    }

    int x_op;
    int y_op;
    int dram_op;
    int x_dir_op;
    std::set<int> cont_layer;
    std::vector<int> cont_core;
    std::vector<int> etc_list;
    
    for(auto &l : place)
    {
        // std::cout << "MK(" << x_MK << "," << y_MK << ") -> DRAM" << dram_MK << " / ";
        if(l.first < 3) // Q, V
        {
            dram_op = layer_DRAM[l.first][2];
            // std::cout << l.first << "(" << x_op <<  "," << y_op << ")->DRAM" << dram_op << ": ";
            if (dram_op < 2)
                x_dir_op = 0; // left
            else
                x_dir_op = 1; // right
            
            if(x_dir_MK == x_dir_op)
            {
                for(auto c : l.second)
                {
                    y_op = c % 6;
                    if(y_op == y_MK)
                    {
                        // std::cout << l.first << ".y(" << dram_op << "):" << y_MK << " / ";
                        cont_core.push_back(c);
                        cont_layer.insert(l.first);
                    }
                }
                cont_layer.insert(l.first);
            }
        }
        else if((l.first % 2 == 1) && (l.first < 19))   // FC-K
        {
            x_op = l.second.front() / 6;
            y_op = l.second.front() % 6;
            // std::cout << l.first << "(" << x_op <<  "," << y_op << "): ";
            if(y_op == y_MK)
            {
                int x_op2 = place[l.first + 1].front() / 6;
                
                if (x_op > x_op2) // left
                    x_dir_op = 0;
                else if (x_op < x_op2)
                    x_dir_op = 1;
                else
                    x_dir_op = -1;

                if (x_dir_op == x_dir_MK)
                {
                    if (x_dir_MK == 0) // left
                    {
                        if (x_op < x_MK || x_op2 < x_MK)
                        {
                            // std::cout << l.first << ":" << x_op << "->" << x_op2 << " / ";
                            cont_core.push_back(l.second.front());
                            cont_layer.insert(l.first);
                        }
                    }
                    else
                    {
                        if (x_op > x_MK || x_op2 > x_MK)
                        {
                            // std::cout << l.first << ":" << x_op << "->" << x_op2 << " / ";
                            cont_core.push_back(l.second.front());
                            cont_layer.insert(l.first);
                        }
                    }
                }
            }
        }
        // std::cout << std::endl;
    }
    // std::cout << "Contention list" << std::endl;
    // for(auto l : cont_layer)
    //     std::cout << l << " / ";
    // std::cout << std::endl;
    for(auto c : cont_core)
    {
        int x = c;
        int y;
        bool isCont = false;
        do
        {
            y = gen() % placement.size();
            if(cont_layer.find(placement[y].layerno) != cont_layer.end())
                isCont = true;
            else
                isCont = false;
        } while (y == cid_MK || x == y || isCont);
        swap(x, y);
    }
    
    bool isCont = true;
    if(cont_core.size() == 0)
        isCont = false;
    
    return isCont;
}

bool Light_placement::spread_WE()
{
    static std::mt19937_64 gen(rand_seed);
    std::unordered_map<lid_t, std::vector<cidx_t>> place;
    cidx_t core_id = 0;

    std::vector<int> y_WE;
    std::unordered_map<int, std::unordered_set<int>> uniq_y_WE;
    std::vector<int> x_WE;
    std::unordered_map<int, std::unordered_set<int>> uniq_x_WE;
    int cid_MK;
    int y_MK;
    int x_MK;
    for (auto &l : placement)
    {
        if(l.layerno == 0)
        {
            y_WE.push_back(core_id % 6);
            uniq_y_WE[core_id % 6].insert(core_id);
            x_WE.push_back(core_id / 6);
            uniq_x_WE[core_id / 6].insert(core_id);
        }
        else if (l.layerno == 19)
        {
            cid_MK = core_id;
            y_MK = core_id % 6;
            x_MK = core_id / 6;
        }
        core_id++;
    }
    bool isCont = false;
    if(uniq_x_WE.size() < 3)
    {
        isCont = true;
        auto tmp_uniq_x_WE = uniq_x_WE;
        for(auto we : tmp_uniq_x_WE)
        {
            if(we.second.size() > 1)
            {
                for(auto c_we : we.second)
                {
                    int x = c_we;
                    int y = c_we;
                    while((uniq_x_WE.find(y / 6) != uniq_x_WE.end()) || ((y/6) == x_MK) || (y % 6) != (x % 6))
                    {
                        y = gen() % placement.size();
                    }
                    swap(x, y);
                    uniq_x_WE[x].erase(c_we);
                    uniq_x_WE[y / 6].insert(c_we);
                }
                
            }
        }
    }

    if(uniq_y_WE.size() < 3)
    {
        isCont = true;
        auto tmp_uniq_y_WE = uniq_y_WE;
        for(auto we : tmp_uniq_y_WE)
        {
            if(we.second.size() > 1)
            {
                for(auto c_we : we.second)
                {
                    int x = c_we;
                    int y = c_we;
                    while ((uniq_y_WE.find(y % 6) != uniq_y_WE.end()) || ((y % 6) == y_MK) || (y / 6) != (x / 6))
                    {
                        y = gen() % placement.size();
                    }
                    swap(x, y);
                    uniq_y_WE[x].erase(c_we);
                    uniq_y_WE[y % 6].insert(c_we);
                }
                
            }
        }
    }

    return isCont;

}

void Light_placement::print()
{
    std::vector<std::vector<Layer_partition>> matrix(Cluster::ylen, std::vector<Layer_partition>(Cluster::xlen));
    cidx_t core_id = 0;
    for (auto &part : placement)
    {
        auto pos = Cluster::get_pos(core_id);
        matrix[pos.y][pos.x] = part;
        ++core_id;
    }
    for (cidx_t i = Cluster::ylen - 1; i >= 0; --i, puts(""))
    {
        for (cidx_t j = 0; j < Cluster::xlen; ++j)
        {
            printf("(%d,%d),", matrix[i][j].layerno, matrix[i][j].partno);
        }
    }
}

std::ostream &operator<<(std::ostream &os, const Light_placement &Light_placement)
{
    std::vector<Light_placement::Layer_partition> cores(36);
    // std::vector<std::vector<Light_placement::Layer_partition> > matrix(Cluster::ylen, std::vector<Light_placement::Layer_partition>(Cluster::xlen));
    cidx_t core_id = 0;

    for (auto &place : Light_placement.placement)
    {
        cores[core_id] = place;
        // auto pos = Cluster::get_pos(core_id);
        // os << pos << "," << part.layerno << " / ";
        core_id++;
    }
    // os << "\n";
    std::map<cidx_t, cidx_t> tmp_part;
    for (auto &part : Light_placement.partition)
    {
        //os << part.layerno << ": " << "b=" << part.b << ",c=" << part.c << ",h=" << part.h << ",w=" << part.w << " / ";
        // os << "part[" << Light_placement.rrnd << "][" << part.layerno << "]={" << part.h << "," << part.c << "};\n";
        
        // os << "part[" << part.layerno << "]={" << part.h << "," << part.c << "};\n";
        core_id = 0;
        tmp_part.clear();
        for (auto &place : cores)
        {

            if (part.layerno == place.layerno)
            {
                tmp_part[place.partno] = core_id;
                // os << core_id << Cluster::get_pos(core_id) << " / ";
            }
            core_id++;
        }

        // os << "assignedDpath[" << Light_placement.rrnd << "][" << part.layerno << "]={";
        // os << "assignedDpath[" << part.layerno << "]={";
        // os << Light_placement.rrnd << ",";
        for (auto &tmp : tmp_part)
        {
            os << tmp.second;
            if(tmp.first != tmp_part.size() - 1)
                os << ",";

            //os << tmp.first << "(" << tmp.second << Cluster::get_pos(tmp.second) << ") / ";
        }
        // os << "};\n";
        os << "\n";
    }
    
    std::map<lid_t, std::vector<mlen_t>> data_dram;
    for (auto &l : Light_placement.layer_DRAM)
    {
        if(l.first < 20)
            data_dram[l.first] = l.second;
    }

    for (auto &l : data_dram)
    {
        data_dram[l.first] = l.second;
        // os << "data_dram[" << Light_placement.rrnd << "][" << l.first << "]={";
        // os << "data_dram[" << l.first << "]={";
        // os << Light_placement.rrnd <<",";
        os << l.second[0] << ",";
        os << l.second[1] << ",";
        os << l.second[2] << "\n";
        // os << l.second[2] << "};";
        
    }

    // for(auto &part: Light_placement.placement){
    //     auto pos = Cluster::get_pos(core_id);
    //     matrix[pos.y][pos.x] = part;
    //     ++core_id;
    // }
    // for(cidx_t i=Cluster::ylen-1;i>=0;--i){
    //     for(cidx_t j=0;j<Cluster::xlen;++j){
    //         os << "(" << matrix[i][j].layerno << ", " << matrix[i][j].partno << "), ";
    //     }
    // }
    return os;
}

// void Light_placement::compare()
// {
    
//     std::vector<Light_placement::Layer_partition> cores(36);
//     cidx_t core_id = 0;

//     for (auto &place : placement)
//     {
//         cores[core_id] = place;
//         core_id++;
//     }

//     std::map<cidx_t, cidx_t> tmp_part;
//     for (auto &part : partition)
//     {
//         gem_p.part[part.layerno] = {part.h, part.c};
        
//         core_id = 0;
//         tmp_part.clear();
//         for (auto &place : cores)
//         {
//             if (part.layerno == place.layerno)
//             {
//                 tmp_part[place.partno] = core_id;
//             }
//             core_id++;
//         }

//         for (auto &tmp : tmp_part)
//         {
//             gem_p.assignedDpath[part.layerno].push_back(tmp.second);
//         }
//     }

//     std::map<lid_t, std::vector<mlen_t>> data_dram;
//     for (auto &l : layer_DRAM)
//     {
//         if(l.first < 20)
//             data_dram[l.first] = l.second;
//     }

//     for (auto &l : data_dram)
//     {
//         for(auto &d : l.second)
//             gem_p.data_dram[l.first].push_back(d);
//     }

//     gem_p.run();
// }