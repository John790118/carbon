////////////////////////////////////////////////////////
// Project： SystemC carbon
// Module:   ing.cpp
// Description: Pkt to cell, look up table then enter the queue (Pkt转Cell，查表入队列)
// Group：
// Author: Sandy
// Date: 2021.10.11  new
// Hierarchy : --top
//             --|--ing.cpp
//    pktgen------>|ing-->sch-->pe-->eng|----->stat
////////////////////////////////////////////////////////
#include "mod_ing.h"

#define mod_ing_stat_print

mod_ing::mod_ing(sc_module_name name):
    sc_module(name)
{
    rr_sch = new RR_SCH(G_INTER_NUM);

    for (int i = 0; i < G_INTER_NUM; i++) {
        in_port[i] = new sc_in<s_pkt_desc>();
        pkt_count_port[i] = 0;
        infifo_count_port[i] = 0;
        drop_count_port[i] = 0;
        dport_pkt_cnt[i] = 0;
        dport_pkt_cell_cnt[i] = 0;
    }
    for (int i = 0; i < 16; i++) {
        que_pkt_cnt[i] = 0;
        que_pkt_cell_cnt[i] = 0;
        flow_pkt_cnt[i] = 0;
        flow_pkt_cell_cnt[i] = 0;
    }
    bcpu_pkt_cnt = 0;

    SC_METHOD(rev_pkt_process);
    for (int i = 0; i < G_INTER_NUM; i++) {
        sensitive << *in_port[i];
    }
    dont_initialize();

    SC_METHOD(main_process);
    sensitive << in_clk_cnt;
    dont_initialize();
}

void mod_ing::main_process()
{
    port_rr_sch_process();
    lut_process();
    pkt_to_cell_process();
}

void mod_ing::rev_pkt_process()
{
    for (int i = 0; i < G_INTER_NUM; i++) {
        if ((*in_port[i]).event()) {
            pkt_count_port[i]++;
            if (fifo_port[i].size() == 16) {
                drop_count_port[i]++;
            } else {
                fifo_port[i].push_back(in_port[i]->read());
                infifo_count_port[i]++;
            }
            MOD_LOG << "ing_in_pkt" << in_port[i]->read();
#ifdef mod_ing_stat_print
            MOD_LOG << "port id:" << i
                    << ", pkts received: " << pkt_count_port[i]
                    << ", pkts infifo: " << infifo_count_port[i]
                    << ", pkts dropped: " << drop_count_port[i];
#endif
        };
    }
}

void mod_ing::port_rr_sch_process()
{
    for (int i = 0; i < G_INTER_NUM; i++) {
        if (fifo_port[i].size() > 0) {
            rr_sch->set_que_valid(i, true); // que非空的时候才参与sch
        } else {
            rr_sch->set_que_valid(i, false);
        }
    }

    int rst_que = -1;
    bool rst_flag = false;

    rst_flag = rr_sch->get_sch_result(rst_que);

    if ((rst_flag == true) and (pkt_out_flag == 0)) {
        s_pkt_desc front_trans = fifo_port[rst_que].front();

        fifo_port[rst_que].pop_front();

        s_port_sch_result = front_trans;

        pkt_tmp_len = front_trans.len;
        pkt_out_flag = 1;
        pkt_head_flag = 1;
        rst_flag = false;
    }
}

void mod_ing::lut_process()
{
    s_hash_rule_key hash_pkt_lut_key;

    hash_pkt_lut_key.sid = s_port_sch_result.sid;
    hash_pkt_lut_key.did = s_port_sch_result.did;
    hash_pkt_lut_key.pri = s_port_sch_result.pri;

    if (pkt_out_flag == 1) {
        auto iter = g_hash_rule_tab.find(hash_pkt_lut_key);
        if (iter != g_hash_rule_tab.end()) {
            flow_id = iter->second;
            flow_rule = g_flow_rule_tab[flow_id];
            que_id = flow_rule.qid;
            dport_id = flow_rule.dport;
            bcpu_flag = 0;

        } else {
            flow_id = -2;
            que_id = -2;
            dport_id = 254;
            MOD_LOG << "hash table err";
            bcpu_flag = 1;
        }
    };
}

void mod_ing::pkt_to_cell_process()
{
    int cell_sn;
    s_pkt_desc cell_trans;
    cell_sn = 0;

    while ((pkt_tmp_len > G_CELL_LEN) && (bcpu_flag == 0)) {
        cell_trans = s_port_sch_result;
        cell_trans.type = 1;
        cell_trans.qid = que_id;
        cell_trans.dport = dport_id;
        cell_trans.fid = flow_id;
        cell_trans.vldl = G_CELL_LEN;
        cell_trans.csn = cell_sn;
        cell_trans.time_stamp.ing_out_clock = g_cycle_cnt;
        if (pkt_head_flag == 1) {
            cell_trans.sop = true;
        } else {
            cell_trans.sop = false;
        }
        cell_trans.eop = false;

        if (out_cell_que.num_free() > 0) {
            out_cell_que.nb_write(cell_trans);
            MOD_LOG << "ing_out_cell" << cell_trans;
        } else {
            MOD_LOG_ERROR << "ing to sch fifo full error!" << cell_trans;
        }
        pkt_tmp_len -= G_CELL_LEN;
        pkt_head_flag = 0;
        cell_sn++;

#ifdef mod_ing_stat_print
        for (int i = 0; i < G_INTER_NUM; i++) {
            if (cell_trans.dport == i) {
                dport_pkt_cell_cnt[i]++;
            }
        }
        for (int i = 0; i < G_QUE_NUM; i++) {
            if (cell_trans.qid == i) {
                que_pkt_cell_cnt[i]++;
            }
        }
        for (int i = 0; i < 16; i++) {
            if (cell_trans.fid == i) {
                flow_pkt_cell_cnt[i]++;
            }
        }
#endif
    }

    if (pkt_tmp_len > 0) {
        cell_trans = s_port_sch_result;
        cell_trans.qid = que_id;
        cell_trans.dport = dport_id;
        cell_trans.fid = flow_id;
        cell_trans.vldl = pkt_tmp_len;
        cell_trans.csn = cell_sn;
        cell_trans.time_stamp.ing_out_clock = g_cycle_cnt;
        if (pkt_head_flag == 1) {
            cell_trans.sop = true;
        } else {
            cell_trans.sop = false;
        }
        cell_trans.eop = true;

        if (bcpu_flag == 0) {
            cell_trans.type = 1;

            if (out_cell_que.num_free() > 0) {
                out_cell_que.nb_write(cell_trans);
                MOD_LOG << "ing_out_cell" << cell_trans;
            } else {
                MOD_LOG_ERROR << "ing to sch fifo full error!" << cell_trans;
            }

        } else {
            cell_trans.type = 0;
            if (out_pkt_bcpu.num_free() > 0) {
                out_pkt_bcpu.nb_write(cell_trans);
                MOD_LOG << "ing_out_bcpu_pkt" << cell_trans;
            } else {
                MOD_LOG_ERROR << "ing to egr bcpu fifo full error!" << cell_trans;
            }
        }

        pkt_tmp_len = 0;
        pkt_out_flag = 0;

#ifdef mod_ing_stat_print
        for (int i = 0; i < G_INTER_NUM; i++) {
            if (cell_trans.dport == i) {
                dport_pkt_cell_cnt[i]++;
                dport_pkt_cnt[i]++;
            }
            MOD_LOG << "dport" << i << "_pkt_cnt:" << dport_pkt_cnt[i] << ", dport" << i << "_pkt_cell_cnt:" << dport_pkt_cell_cnt[i] << endl;
        }
        for (int i = 0; i < G_QUE_NUM; i++) {
            if (cell_trans.qid == i) {
                que_pkt_cell_cnt[i]++;
                que_pkt_cnt[i]++;
            }
            MOD_LOG << "que" << i << "_pkt_cnt:" << que_pkt_cnt[i] << ", que" << i << "_pkt_cell_cnt" << que_pkt_cell_cnt[i] << endl;
        }
        for (int i = 0; i < 16; i++) {
            if (cell_trans.fid == i) {
                flow_pkt_cell_cnt[i]++;
                flow_pkt_cnt[i]++;
            }
            MOD_LOG << "flow" << i << "_pkt_cnt:" << flow_pkt_cnt[i] << ", flow" << i << "_pkt_cell_cnt" << flow_pkt_cell_cnt[i] << endl;
        }
        if (cell_trans.dport == 254) {
            bcpu_pkt_cnt++;
            MOD_LOG << "bcpu_pkt_cnt:" << bcpu_pkt_cnt;
        }

#endif
    }
}
