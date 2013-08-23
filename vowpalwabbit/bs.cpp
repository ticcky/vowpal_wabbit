/*
Copyright (c) by respective owners including Yahoo!, Microsoft, and
individual contributors. All rights reserved.  Released under a BSD (revised)
license as described in the file LICENSE.
 */
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <sstream>
#include <boost/random/poisson_distribution.hpp>
#include <boost/random/mersenne_twister.hpp>
#include <boost/random/variate_generator.hpp>
#include <numeric>
#include <vector>
#include <algorithm>

#include "bs.h"
#include "simple_label.h"
#include "cache.h"
#include "v_hashmap.h"
#include "vw.h"
#include "rand48.h"

using namespace std;

namespace BS {

  struct bs{
    uint32_t k;
    uint32_t increment;
    uint32_t total_increment;
    float alpha;
    learner base;
    vw* all;
  };

  char* bufread_label(mc_label* ld, char* c)
  {
    ld->label = *(float *)c;
    c += sizeof(ld->label);
    ld->weight = *(float *)c;
    c += sizeof(ld->weight);
    return c;
  }

  size_t read_cached_label(shared_data*, void* v, io_buf& cache)
  {
    mc_label* ld = (mc_label*) v;
    char *c;
    size_t total = sizeof(ld->label)+sizeof(ld->weight);
    if (buf_read(cache, c, total) < total) 
      return 0;
    c = bufread_label(ld,c);

    return total;
  }

  float weight(void* v)
  {
    mc_label* ld = (mc_label*) v;
    return (ld->weight > 0) ? ld->weight : 0.f;
  }

  float initial(void* v)
  {
    return 0.;
  }

  char* bufcache_label(mc_label* ld, char* c)
  {
    *(float *)c = ld->label;
    c += sizeof(ld->label);
    *(float *)c = ld->weight;
    c += sizeof(ld->weight);
    return c;
  }

  void cache_label(void* v, io_buf& cache)
  {
    char *c;
    mc_label* ld = (mc_label*) v;
    buf_write(cache, c, sizeof(ld->label)+sizeof(ld->weight));
    c = bufcache_label(ld,c);
  }

  void default_label(void* v)
  {
    mc_label* ld = (mc_label*) v;
    ld->label = -1;
    ld->weight = 1.;
  }

  void delete_label(void* v)
  {
  }

  void parse_label(parser* p, shared_data*, void* v, v_array<substring>& words)
  {
    mc_label* ld = (mc_label*)v;

    switch(words.size()) {
    case 0:
      break;
    case 1:
      ld->label = (float)int_of_substring(words[0]);
      ld->weight = 1.0;
      break;
    case 2:
      ld->label = (float)int_of_substring(words[0]);
      ld->weight = float_of_substring(words[1]);
      break;
    default:
      cerr << "malformed example!\n";
      cerr << "words.size() = " << words.size() << endl;
    }
  }

  void print_update(vw& all, example *ec)
  {
    if (all.sd->weighted_examples > all.sd->dump_interval && !all.quiet && !all.bfgs)
      {
        mc_label* ld = (mc_label*) ec->ld;
        char label_buf[32];
        if (ld->label == INT_MAX)
          strcpy(label_buf," unknown");
        else
          sprintf(label_buf,"%8ld",(long int)ld->label);

        fprintf(stderr, "%-10.6f %-10.6f %8ld %8.1f   %s %8ld %8lu\n",
                all.sd->sum_loss/all.sd->weighted_examples,
                all.sd->sum_loss_since_last_dump / (all.sd->weighted_examples - all.sd->old_weighted_examples),
                (long int)all.sd->example_number,
                all.sd->weighted_examples,
                label_buf,
                (long int)ec->final_prediction,
                (long unsigned int)ec->num_features);
     
        all.sd->sum_loss_since_last_dump = 0.0;
        all.sd->old_weighted_examples = all.sd->weighted_examples;
        all.sd->dump_interval *= 2;
      }
  }

  void output_example(vw& all, example* ec)
  {
    if (command_example(&all,ec))
      return;

    mc_label* ld = (mc_label*)ec->ld;
    all.sd->weighted_examples += ld->weight;
    all.sd->total_features += ec->num_features;
    size_t loss = 1;
    if (ld->label == ec->final_prediction)
      loss = 0;
    all.sd->sum_loss += loss;
    all.sd->sum_loss_since_last_dump += loss;
  
    for (int* sink = all.final_prediction_sink.begin; sink != all.final_prediction_sink.end; sink++)
      all.print(*sink, ec->final_prediction, 0, ec->tag);
  
    all.sd->example_number++;

    print_update(all, ec);
  }

  void learn_with_output(bs* d, example* ec, bool shouldOutput)
  {
    vw* all = d->all;
    if (command_example(all,ec))
      {
	d->base.learn(ec);
	return;
      }

    mc_label* mc_label_data = (mc_label*)ec->ld;
  
    string outputString;
    stringstream outputStringStream(outputString);

    float pre_mean = 0.0;
    float tmp = frand48()*1000;

    boost::mt19937 gen;
    gen.seed(tmp);
    boost::poisson_distribution<int> pd(((label_data*)ec->ld)->weight);
    boost::variate_generator <boost::mt19937, boost::poisson_distribution<int> > rvt(gen, pd);
    vector<double> pred_vec;

    for (size_t i = 1; i <= d->k; i++)
      {
        if (i != 1)
          update_example_indicies(all->audit, ec, d->increment);
          
        ((label_data*)ec->ld)->weight = rvt();

        d->base.learn(ec);

        pred_vec.push_back(ec->partial_prediction);

        if (shouldOutput) {
          if (i > 1) outputStringStream << ' ';
          outputStringStream << i << ':' << ec->partial_prediction;
        }

        ec->partial_prediction = 0.;
      }	
    ec->ld = mc_label_data;

    update_example_indicies(all->audit, ec, -d->total_increment);
 
    sort(pred_vec.begin(), pred_vec.end());
    
    pre_mean = accumulate(pred_vec.begin(), pred_vec.end(), 0.0)/pred_vec.size();
    ec->final_prediction = pre_mean;

    if(!all->training){
      cout<<"mean: "<<pre_mean<<endl;
      size_t lb_index = d->k * d->alpha-1 < 0 ? 0 :  d->k * d->alpha-1;
      size_t up_index = d->k * (1 - d->alpha)-1 > pred_vec.size()-1 ? pred_vec.size()-1 : d->k * (1 - d->alpha)-1;
      cout<< (1- d->alpha)<<" percentile: ("<<pred_vec[lb_index]<<", "<<pred_vec[up_index]<<")"<<endl;
    }

    if (shouldOutput) 
      all->print_text(all->raw_prediction, outputStringStream.str(), ec->tag);
  }

  void learn(void* d, example* ec) {
    learn_with_output((bs*)d, ec, false);
  }

  void drive(vw* all, void* d)
  {
    example* ec = NULL;
    while ( true )
      {
        if ((ec = VW::get_example(all->p)) != NULL)//semiblocking operation.
          {
            learn_with_output((bs*)d, ec, all->raw_prediction > 0);
	    if (!command_example(all, ec))
	      output_example(*all, ec);
	    VW::finish_example(*all, ec);
          }
        else if (parser_done(all->p))
	  return;
        else 
          ;
      }
  }

  void finish(void* data)
  {    
    bs* o=(bs*)data;
    o->base.finish();
    free(o);
  }

  learner setup(vw& all, std::vector<std::string>&opts, po::variables_map& vm, po::variables_map& vm_file)
  {
    bs* data = (bs*)calloc(1, sizeof(bs));
    data->alpha = 0.;

    po::options_description desc("BS options");
    desc.add_options()
      ("bs_percentile", po::value<float>(), "percentile for confidence interval");

    po::parsed_options parsed = po::command_line_parser(opts).
      style(po::command_line_style::default_style ^ po::command_line_style::allow_guessing).
      options(desc).allow_unregistered().run();
    opts = po::collect_unrecognized(parsed.options, po::include_positional);
    po::store(parsed, vm);
    po::notify(vm);

    po::parsed_options parsed_file = po::command_line_parser(all.options_from_file_argc,all.options_from_file_argv).
      style(po::command_line_style::default_style ^ po::command_line_style::allow_guessing).
      options(desc).allow_unregistered().run();
    po::store(parsed_file, vm_file);
    po::notify(vm_file);


    if (vm.count("bs_percentile") || vm_file.count("bs_percentile"))
    {
      if(vm_file.count("bs_percentile"))
        data->alpha = 1 - vm_file["bs_percentile"].as<float>();
      else {
        data->alpha = 1 - vm["bs_percentile"].as<float>();
        std::stringstream ss;
        ss << " --bs_percentile " << vm["bs_percentile"].as<float>();
        all.options_from_file.append(ss.str());
      }
      if(data->alpha > 1 || data->alpha < 0)
        std::cerr << "warning: bs_percentile should be between 0 and 1 !"<< endl;
    }

    if( vm_file.count("bs") ) {
      data->k = (uint32_t)vm_file["bs"].as<size_t>();
      if( vm.count("bs") && (uint32_t)vm["bs"].as<size_t>() != data->k )
        std::cerr << "warning: you specified a different number of actions through --bs than the one loaded from predictor. Pursuing with loaded value of: " << data->k << endl;
    }
    else {
      data->k = (uint32_t)vm["bs"].as<size_t>();

      //append bs with nb_actions to options_from_file so it is saved to regressor later
      std::stringstream ss;
      ss << " --bs " << data->k;
      all.options_from_file.append(ss.str());
    }

    data->all = &all;
    *(all.p->lp) = mc_label_parser;
    data->increment = all.reg.stride * all.weights_per_problem;
    all.weights_per_problem *= data->k;
    data->total_increment = data->increment*(data->k-1);
    data->base = all.l;
    learner l(data, drive, learn, finish, all.l.sl);
    return l;
  }
}
