/*
Copyright (c) by respective owners including Yahoo!, Microsoft, and
individual contributors. All rights reserved.  Released under a BSD
license as described in the file LICENSE.
 */
#ifndef SEARN_PYTHONTASK_H
#define SEARN_PYTHONTASK_H

#include "searn.h"

namespace PythonTask {
  void initialize(Searn::searn&, size_t&, po::variables_map&);
  void finish(Searn::searn&);
  void structured_predict(Searn::searn&, vector<example*>&);
  extern Searn::searn_task task;

  struct task_data {
    void (*run_f)(Searn::searn&);
    void *run_object;                  // this will really be a (py::object*), but we don't want basic VW to have to know about python
    void (*delete_run_object)(void*);  // we can't delete run_object on our own because we don't know its size, so provide a hook
    po::variables_map* var_map;        // so that python can access command line variables
    size_t num_actions;                // cache for easy access
  };
}

#endif
