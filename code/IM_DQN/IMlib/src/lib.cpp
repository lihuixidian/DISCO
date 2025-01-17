#include "config.h"
#include "lib.h"
#include "graph.h"
#include "nn_api.h"
#include "qnet.h"
#include "nstep_replay_mem.h"
#include "simulator.h"
#include "env.h"
#include <random>
#include <algorithm>
#include <cstdlib>
#include <signal.h>
#include <time.h>
#include <sys/time.h>
using namespace gnn;

void intHandler(int dummy) {
    exit(0);
}

int LoadModel(const char* filename)
{
    ASSERT(net, "please init the lib before use");    
    net->model.Load(filename);
    return 0;
}

int SaveModel(const char* filename)
{
    ASSERT(net, "please init the lib before use");
    net->model.Save(filename);
    return 0;
}

std::vector< std::vector<double>* > list_pred;
Env* test_env;
int Init(const int argc, const char** argv)
{
    signal(SIGINT, intHandler);
    
    cfg::LoadParams(argc, argv);
    GpuHandle::Init(1, 1);

    net = new QNet();    
    net->BuildNet();

    NStepReplayMem::Init(50000000);
    
    Simulator::Init(cfg::num_env);
    for (int i = 0; i < cfg::num_env; ++i)
        Simulator::env_list[i] = new Env(cfg::max_n);
    test_env = new Env(cfg::max_n);

    list_pred.resize(cfg::batch_size);
    for (int i = 0; i < cfg::batch_size; ++i)
        list_pred[i] = new std::vector<double>(400000);
    return 0;
}

int UpdateSnapshot()
{
    net->old_model.DeepCopyFrom(net->model);
    return 0;
}

int InsertGraph(bool isTest, const int g_id, const int num_nodes, const int num_edges, const int* edges_from, const int* edges_to)//weight
{
    auto g = std::make_shared<Graph>(num_nodes, num_edges, edges_from, edges_to); //weight
    if (isTest)
    {
        GSetTest.InsertGraph(g_id, g);
    }
        
    else
        GSetTrain.InsertGraph(g_id, g);
    return 0;
}

int ClearTrainGraphs()
{
    GSetTrain.graph_pool.clear();
    return 0;
}

int PlayGame(const int n_traj, const double eps)
{
    Simulator::run_simulator(n_traj, eps);
    return 0;
}

ReplaySample sample;
std::vector<double> list_target;
double Fit()
{
    NStepReplayMem::Sampling(cfg::batch_size, sample);
    bool ness = false;
    for (int i = 0; i < cfg::batch_size; ++i)
        if (!sample.list_term[i])
        {
            ness = true;
            break;
        }
    if (ness)
        PredictWithSnapshot(sample.g_list, sample.list_s_primes, list_pred);
    
    list_target.resize(cfg::batch_size);

    for (int i = 0; i < cfg::batch_size; ++i)
    {
        double q_rhs = 0;
        if (!sample.list_term[i])
            q_rhs = max(sample.g_list[i]->num_nodes, list_pred[i]->data());
        q_rhs += sample.list_rt[i];
        list_target[i] = q_rhs;
    }

    return Fit(sample.g_list, sample.list_st, sample.list_at, list_target);
}

double Test(const int gid)
{
    std::vector< std::shared_ptr<Graph> > g_list(1);
    std::vector< std::vector<int>* > states(1);

    test_env->s0(GSetTest.Get(gid));
    states[0] = &(test_env->action_list);
    g_list[0] = test_env->graph;

    double cost = 0;
    int new_action;
    while (!test_env->isTerminal())
    {
        cost++;

        Predict(g_list, states, list_pred);
        new_action = arg_max(test_env->graph->num_nodes, list_pred[0]->data());
        test_env->step(new_action);
    }
    return test_env->numCoveredEdges;
}

double GetSol(const int gid, int* sol)
{   
    std::vector< std::shared_ptr<Graph> > g_list(1);
    std::vector< std::vector<int>* > states(1);
    test_env->s0(GSetTest.Get(gid));
    states[0] = &(test_env->action_list);
    g_list[0] = test_env->graph;

    int new_action;
    int len = 0;

    while (!test_env->isTerminal())
    {   
        Predict(g_list, states, list_pred);
        new_action = arg_max(test_env->graph->num_nodes, list_pred[0]->data());
            
        test_env->step(new_action);
        len++;
        sol[len] = new_action;
    }

    sol[0] = len;
    return test_env->influenced_set;    
}

double GetSol_test(const int gid, int* sol)
{
    std::vector< std::shared_ptr<Graph> > g_list(1);
    std::vector< std::vector<int>* > states(1);
    test_env->s0(GSetTest.Get(gid));
    states[0] = &(test_env->action_list);
    g_list[0] = test_env->graph;

    int new_action;
    int len = 0;

    while (!test_env->isTerminal())
    {   
        Predict(g_list, states, list_pred);
        new_action = arg_max(test_env->graph->num_nodes, list_pred[0]->data());
        test_env->step_test(new_action);
        len++;
        sol[len] = new_action;       
        for(unsigned int i=0;i<cfg::seed_k-1;i++)
        {
            Predict_test(g_list, states, list_pred);
            int new_action_for;
            new_action_for = arg_max(test_env->graph->num_nodes, list_pred[0]->data());
            test_env->step_test(new_action_for);
            len++;
            sol[len] = new_action_for;

        }
    }
    sol[0] = len;
    return test_env->influenced_set;    
}
