#include "mod_stim.h"

void port_fifo::pkt_in(const s_pkt_desc &data_pkt)
{
    regs[pntr++] = data_pkt;
    empty = false;
    if (pntr == (int)g_flow_rule_tab.size())
        full = true;
}

s_pkt_desc port_fifo::pkt_out()
{
    s_pkt_desc temp;
    temp = regs[0];
    if (--pntr == 0)
        empty = true;
    else
        for (int i = 0; i < (int)g_flow_rule_tab.size(); i++)
            regs[i] = regs[i + 1];
    return (temp);
}

s_pkt_desc port_fifo::pkt_pre_val()
{
    s_pkt_desc temp;
    temp = regs[0];
    return (temp);
}

void token_bucket::add_token(const int &add_token_val)
{
    if ((token + add_token_val) > TOKEN_MAX_BYTE)
        token = TOKEN_MAX_BYTE;
    else
        token = token + add_token_val;
}

void token_bucket::sub_token(const int &sub_token_val)
{
    if ((token - sub_token_val) < 0)
        token = 0;
    else
        token = token - sub_token_val;
}

int token_bucket::read_token()
{
    int temp;
    temp = token;
    return (temp);
}

mod_stim::mod_stim(sc_module_name name, func_stat *base_top_stat):
    sc_module(name)
{
    top_stat = base_top_stat;
    //    pkt_sender_file.open("pkt_sender_file.log");
    pkt_sender_file.open(pkt_sender_filename);
    flow_sent_pkts.resize(g_flow_rule_tab.size());
    flow_sent_bytes.resize(g_flow_rule_tab.size());
    flow_dpd_pkts.resize(g_flow_rule_tab.size());
    flow_dpd_bytes.resize(g_flow_rule_tab.size());
    flow_sent_mbps.resize(g_flow_rule_tab.size());
    port_sent_pkts.resize(G_INTER_NUM);
    port_sent_bytes.resize(G_INTER_NUM);
    port_dpd_pkts.resize(G_INTER_NUM);
    port_dpd_bytes.resize(G_INTER_NUM);
    port_sent_mbps.resize(G_INTER_NUM);

    flow_rule_nomatch.sid = -1;
    flow_rule_nomatch.did = 0;
    flow_rule_nomatch.len = 64;
    flow_rule_nomatch.pri = 0;
    flow_rule_nomatch.sport = 3;
    flow_rule_nomatch.dport = 0;
    flow_rule_nomatch.qid = -1;
    flow_rule_nomatch.len2add = -1;
    flow_rule_nomatch.flow_speed = 0;

    SC_THREAD(stim_prc);
    sensitive << in_clk_cnt;
}

mod_stim::~mod_stim()
{
    for (int i = 0; i < (int)g_flow_rule_tab.size(); i++) {
        pkt_sender_file << "@" << in_clk_cnt << ":flow [" << i << "] total drop packets:" << flow_dpd_pkts[i] << endl;
        pkt_sender_file << "@" << in_clk_cnt << ":flow [" << i << "] total drop bytes  :" << flow_dpd_bytes[i] << endl;
        pkt_sender_file << "@" << in_clk_cnt << ":flow [" << i << "] total send packets:" << flow_sent_pkts[i] << endl;
        pkt_sender_file << "@" << in_clk_cnt << ":flow [" << i << "] total send bytes  :" << flow_sent_bytes[i] << endl;
        pkt_sender_file << "@" << in_clk_cnt << ":flow [" << i << "] send speed(MBPS)  :" << flow_sent_mbps[i] << endl;
    }
    if (1) {
        pkt_sender_file << "@" << in_clk_cnt << ":flow no match total drop packets:" << flow_nomatch_dpd_pkts << endl;
        pkt_sender_file << "@" << in_clk_cnt << ":flow no match total drop bytes  :" << flow_nomatch_dpd_bytes << endl;
        pkt_sender_file << "@" << in_clk_cnt << ":flow no match total send packets:" << flow_nomatch_sent_pkts << endl;
        pkt_sender_file << "@" << in_clk_cnt << ":flow no match total send bytes  :" << flow_nomatch_sent_bytes << endl;
        pkt_sender_file << "@" << in_clk_cnt << ":flow no match send speed(MBPS)  :" << flow_nomatch_sent_mbps << endl;
    }
    for (int i = 0; i < G_INTER_NUM; i++) {
        pkt_sender_file << "@" << in_clk_cnt << ":port [" << i << "] total drop packets:" << port_dpd_pkts[i] << endl;
        pkt_sender_file << "@" << in_clk_cnt << ":port [" << i << "] total drop bytes  :" << port_dpd_bytes[i] << endl;
        pkt_sender_file << "@" << in_clk_cnt << ":port [" << i << "] total send packets:" << port_sent_pkts[i] << endl;
        pkt_sender_file << "@" << in_clk_cnt << ":port [" << i << "] total send bytes  :" << port_sent_bytes[i] << endl;
        pkt_sender_file << "@" << in_clk_cnt << ":port [" << i << "] send speed(MBPS)  :" << port_sent_mbps[i] << endl;
    }
    pkt_sender_file.flush();
    pkt_sender_file.close();
}

void mod_stim::stim_prc()
{
    int pkt_send_count;
    int send_pkt_port;
    int token_count;
    array<port_fifo, G_INTER_NUM> port_fifo_inst;
    array<token_bucket, G_INTER_NUM> port_token_bucket;
    vector<token_bucket> flow_token_bucket;
    token_bucket flow_nomatch_token_bucket;
    vector<int> flow_sn;
    int flow_nomatch_sn;
    s_pkt_desc pkt_desc_tmp;

    pkt_send_count = 0;
    token_count = 0;

    flow_token_bucket.resize(g_flow_rule_tab.size());
    flow_sn.resize(g_flow_rule_tab.size());

    for (int i = 0; i < G_INTER_NUM; i++) {
        port_fifo_inst[i].pntr = 0;
        port_fifo_inst[i].full = false;
        port_fifo_inst[i].empty = true;
    }

    for (int i = 0; i < G_INTER_NUM; i++) {
        port_token_bucket[i].token = 0;
    }

    for (int i = 0; i < (int)g_flow_rule_tab.size(); i++) {
        flow_token_bucket[i].token = 0;
    }
    flow_nomatch_token_bucket.token = 0;

    for (int i = 0; i < (int)g_flow_rule_tab.size(); i++) {
        flow_sn[i] = 0;
    }
    flow_nomatch_sn = 0;

    wait(1);
    while (1) {
        if (token_count < G_FREQ_MHZ) {
            token_count++;
        } else {
            token_count = 0;
            // add token to port token bucket
            for (int i = 0; i < G_INTER_NUM; i++) {
                port_token_bucket[i].add_token(g_port_rule_tab[i]); // 1000Mbps=125MBPS
            }
            // add token to flow token bucket
            for (int i = 0; i < (int)g_flow_rule_tab.size(); i++) {
                flow_token_bucket[i].add_token(g_flow_rule_tab[i].flow_speed);
            }
            flow_nomatch_token_bucket.add_token(flow_rule_nomatch.flow_speed);
        }

        // generate desc packet per flow
        for (int fid = 0; fid < (int)g_flow_rule_tab.size(); fid++) {
            send_pkt_port = g_flow_rule_tab[fid].sport;

            pkt_desc_tmp.type = 0;
            pkt_desc_tmp.fid = fid;
            pkt_desc_tmp.sid = g_flow_rule_tab[fid].sid;
            pkt_desc_tmp.did = g_flow_rule_tab[fid].did;
            pkt_desc_tmp.fsn = flow_sn[fid];
            pkt_desc_tmp.len = g_flow_rule_tab[fid].len;
            pkt_desc_tmp.pri = g_flow_rule_tab[fid].pri;
            pkt_desc_tmp.sport = g_flow_rule_tab[fid].sport;
            pkt_desc_tmp.dport = g_flow_rule_tab[fid].dport;
            pkt_desc_tmp.qid = g_flow_rule_tab[fid].qid;
            pkt_desc_tmp.vldl = -1;
            pkt_desc_tmp.csn = -1;
            pkt_desc_tmp.sop = true;
            pkt_desc_tmp.eop = true;

            if (flow_token_bucket[fid].read_token() >= pkt_desc_tmp.len) {
                flow_token_bucket[fid].sub_token(pkt_desc_tmp.len);
                if (port_fifo_inst[send_pkt_port].full == true) {
                    flow_dpd_pkts[fid]++;
                    flow_dpd_bytes[fid] = flow_dpd_bytes[fid] + pkt_desc_tmp.len;
                    port_dpd_pkts[send_pkt_port]++;
                    port_dpd_bytes[send_pkt_port] = port_dpd_bytes[send_pkt_port] + pkt_desc_tmp.len;
                } else {
                    port_fifo_inst[send_pkt_port].pkt_in(pkt_desc_tmp);
                    flow_sn[fid] = flow_sn[fid] + 1;
                    flow_sent_pkts[fid]++;
                    flow_sent_bytes[fid] = flow_sent_bytes[fid] + pkt_desc_tmp.len;
                    port_sent_pkts[send_pkt_port]++;
                    port_sent_bytes[send_pkt_port] = port_sent_bytes[send_pkt_port] + pkt_desc_tmp.len;
                }
                pkt_send_count++;
            }
            flow_sent_mbps[fid] = (flow_sent_bytes[fid] * G_FREQ_MHZ) / in_clk_cnt;
            port_sent_mbps[send_pkt_port] = (port_sent_bytes[send_pkt_port] * G_FREQ_MHZ) / in_clk_cnt;
        }
        //
        // generate desc packet to bcpu
        if (0) {
            send_pkt_port = flow_rule_nomatch.sport;

            pkt_desc_tmp.type = 0;
            pkt_desc_tmp.fid = -1;
            pkt_desc_tmp.sid = flow_rule_nomatch.sid;
            pkt_desc_tmp.did = flow_rule_nomatch.did;
            pkt_desc_tmp.fsn = flow_nomatch_sn;
            pkt_desc_tmp.len = flow_rule_nomatch.len;
            pkt_desc_tmp.pri = flow_rule_nomatch.pri;
            pkt_desc_tmp.sport = flow_rule_nomatch.sport;
            pkt_desc_tmp.dport = -1;
            pkt_desc_tmp.qid = -1;
            pkt_desc_tmp.vldl = -1;
            pkt_desc_tmp.csn = -1;
            pkt_desc_tmp.sop = true;
            pkt_desc_tmp.eop = true;

            if (flow_nomatch_token_bucket.read_token() >= pkt_desc_tmp.len) {
                flow_nomatch_token_bucket.sub_token(pkt_desc_tmp.len);
                if (port_fifo_inst[send_pkt_port].full == true) {
                    flow_nomatch_dpd_pkts++;
                    flow_nomatch_dpd_bytes = flow_nomatch_dpd_bytes + pkt_desc_tmp.len;
                    port_dpd_pkts[send_pkt_port]++;
                    port_dpd_bytes[send_pkt_port] = port_dpd_bytes[send_pkt_port] + pkt_desc_tmp.len;
                } else {
                    port_fifo_inst[send_pkt_port].pkt_in(pkt_desc_tmp);
                    flow_nomatch_sn = flow_nomatch_sn + 1;
                    flow_nomatch_sent_pkts++;
                    flow_nomatch_sent_bytes = flow_nomatch_sent_bytes + pkt_desc_tmp.len;
                    port_sent_pkts[send_pkt_port]++;
                    port_sent_bytes[send_pkt_port] = port_sent_bytes[send_pkt_port] + pkt_desc_tmp.len;
                }
                pkt_send_count++;
            }
            flow_nomatch_sent_mbps = (flow_nomatch_sent_bytes * G_FREQ_MHZ) / in_clk_cnt;
            port_sent_mbps[send_pkt_port] = (port_sent_bytes[send_pkt_port] * G_FREQ_MHZ) / in_clk_cnt;
        }

        // output desc packet to each port
        for (int send_port = 0; send_port < G_INTER_NUM; send_port++) {
            //output pkt_data
            pkt_desc_tmp = port_fifo_inst[send_port].pkt_pre_val();
            if ((port_fifo_inst[send_port].empty == false)
                && (port_token_bucket[send_port].read_token() >= pkt_desc_tmp.len)) {
                pkt_desc_tmp = port_fifo_inst[send_port].pkt_out();
                pkt_desc_tmp.time_stamp.stm_out_clock = g_cycle_cnt;
                port_token_bucket[send_port].sub_token(pkt_desc_tmp.len);
                out_pkt_stim[send_port].write(pkt_desc_tmp);
                top_stat->output_comm_stat_func(pkt_desc_tmp);
                MOD_LOG << "@" << in_clk_cnt << "_clks stim sent =>:"
                        << "sport:" << send_port << pkt_desc_tmp;
                pkt_sender_file << "@" << in_clk_cnt << "_clks stim sent =>:"
                                << "sport:" << send_port << pkt_desc_tmp << endl;
                //清零
                pkt_desc_tmp.dport = -1;
                pkt_desc_tmp.qid = -1;
            }
        }

        wait();
    }
}
