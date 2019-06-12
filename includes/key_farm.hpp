/******************************************************************************
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU Lesser General Public License version 3 as
 *  published by the Free Software Foundation.
 *  
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 *  License for more details.
 *  
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software Foundation,
 *  Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 ******************************************************************************
 */

/** 
 *  @file    key_farm.hpp
 *  @author  Gabriele Mencagli
 *  @date    17/10/2017
 *  
 *  @brief Key_Farm pattern executing a windowed transformation in parallel
 *         on multi-core CPUs
 *  
 *  @section Key_Farm (Description)
 *  
 *  This file implements the Key_Farm pattern able to execute windowed queries on a
 *  multicore. The pattern executes streaming windows in parallel on the CPU cores
 *  and supports both a non-incremental and an incremental query definition. Only
 *  windows belonging to different sub-streams can be executed in parallel, while
 *  windows of the same sub-stream are executed rigorously in order.
 *  
 *  The template parameters tuple_t and result_t must be default constructible, with
 *  a copy Constructor and copy assignment operator, and they must provide and implement
 *  the setControlFields() and getControlFields() methods.
 */ 

#ifndef KEY_FARM_H
#define KEY_FARM_H

// includes
#include <ff/farm.hpp>
#include <ff/optimize.hpp>
#include <win_seq.hpp>
#include <kf_nodes.hpp>
#include <pane_farm.hpp>
#include <win_mapreduce.hpp>

/** 
 *  \class Key_Farm
 *  
 *  \brief Key_Farm pattern executing a windowed transformation in parallel on multi-core CPUs
 *  
 *  This class implements the Key_Farm pattern executing windowed queries in parallel on
 *  a multicore. In the pattern, only windows belonging to different sub-streams can be
 *  executed in parallel.
 */ 
template<typename tuple_t, typename result_t, typename input_t>
class Key_Farm: public ff_farm
{
public:
    /// type of the non-incremental window processing function
    using win_func_t = function<void(uint64_t, Iterable<tuple_t> &, result_t &)>;
    /// type of the rich non-incremental window processing function
    using rich_win_func_t = function<void(uint64_t, Iterable<tuple_t> &, result_t &, RuntimeContext &)>;
    /// type of the incremental window processing function
    using winupdate_func_t = function<void(uint64_t, const tuple_t &, result_t &)>;
    /// type of the rich incremental window processing function
    using rich_winupdate_func_t = function<void(uint64_t, const tuple_t &, result_t &, RuntimeContext &)>;
    /// type of the closing function
    using closing_func_t = function<void(RuntimeContext &)>;
    /// type of the Pane_Farm used for the nesting Constructor
    using pane_farm_t = Pane_Farm<tuple_t, result_t>;
    /// type of the Win_MapReduce used for the nesting Constructor
    using win_mapreduce_t = Win_MapReduce<tuple_t, result_t>;
    /// type of the functionto map the key hashcode onto an identifier starting from zero to pardegree-1
    using routing_func_t = function<size_t(size_t, size_t)>;
private:
    // type of the wrapper of input tuples
    using wrapper_in_t = wrapper_tuple_t<tuple_t>;
    // type of the KF_Emitter node
    using kf_emitter_t = KF_Emitter<tuple_t, input_t>;
    // type of the KF_Collector node
    using kf_collector_t = KF_NestedCollector<result_t>;
    // type of the Win_Seq to be created within the regular Constructor
    using win_seq_t = Win_Seq<tuple_t, result_t, wrapper_in_t>;
    // friendships with other classes in the library
    template<typename T>
    friend auto get_KF_nested_type(T);
    // flag stating whether the Key_Farm has been instantiated with complex workers (Pane_Farm or Win_MapReduce instances)
    bool hasComplexWorkers;
    // optimization level of the Key_Farm
    opt_level_t opt_level;
    // window type (CB or TB)
    win_type_t winType;

    // Private Constructor I (stub)
    Key_Farm() {}

    // Private Constructor II
    template<typename F_t>
    Key_Farm(F_t _func,
             uint64_t _win_len,
             uint64_t _slide_len,
             win_type_t _winType,
             size_t _pardegree,
             string _name,
             closing_func_t _closing_func,
             routing_func_t _routing_func,
             opt_level_t _opt_level,
             PatternConfig _config,
             role_t _role)
             :
             hasComplexWorkers(false),
             opt_level(_opt_level),
             winType(_winType)
    {
        // check the validity of the windowing parameters
        if (_win_len == 0 || _slide_len == 0) {
            cerr << RED << "WindFlow Error: window length or slide cannot be zero" << DEFAULT << endl;
            exit(EXIT_FAILURE);
        }
        // check the validity of the parallelism degree
        if (_pardegree == 0) {
            cerr << RED << "WindFlow Error: parallelism degree cannot be zero" << DEFAULT << endl;
            exit(EXIT_FAILURE);
        }
        // check the optimization level
        if (_opt_level != LEVEL0) {
            cerr << YELLOW << "WindFlow Warning: optimization level has no effect" << DEFAULT << endl;
            opt_level = LEVEL0;
        }
        // vector of Win_Seq instances
        vector<ff_node *> w(_pardegree);
        // create the Win_Seq instances
        for (size_t i = 0; i < _pardegree; i++) {
            PatternConfig configSeq(0, 1, _slide_len, 0, 1, _slide_len);
            auto *seq = new win_seq_t(_func, _win_len, _slide_len, _winType, _name + "_kf", _closing_func, RuntimeContext(_pardegree, i), configSeq, SEQ);
            w[i] = seq;
        }
        ff_farm::add_workers(w);
        ff_farm::add_collector(nullptr);
        // create the Emitter node
        ff_farm::add_emitter(new kf_emitter_t(_routing_func, _pardegree));
        // when the Key_Farm will be destroyed we need aslo to destroy the emitter, workers and collector
        ff_farm::cleanup_all();
    }

    // method to optimize the structure of the Key_Farm pattern
    void optimize_KeyFarm(opt_level_t opt)
    {
        if (opt == LEVEL0) // no optimization
            return;
        else if (opt == LEVEL1 || opt == LEVEL2) // optimization level 1
            remove_internal_collectors(*this); // remove all the default collectors in the Win_Farm
        else { // optimization level 2
            cerr << YELLOW << "WindFlow Warning: optimization level not supported yet" << DEFAULT << endl;
            assert(false);
        }
    }

public:
    /** 
     *  \brief Constructor I
     *  
     *  \param _win_func the non-incremental window processing function
     *  \param _win_len window length (in no. of tuples or in time units)
     *  \param _slide_len slide length (in no. of tuples or in time units)
     *  \param _winType window type (count-based CB or time-based TB)
     *  \param _pardegree parallelism degree of the Key_Farm pattern
     *  \param _name string with the unique name of the pattern
     *  \param _closing_func closing function
     *  \param _routing_func function to map the key hashcode onto an identifier starting from zero to pardegree-1
     *  \param _opt_level optimization level used to build the pattern
     */ 
    Key_Farm(win_func_t _win_func,
             uint64_t _win_len,
             uint64_t _slide_len,
             win_type_t _winType,
             size_t _pardegree,
             string _name,
             closing_func_t _closing_func,
             routing_func_t _routing_func,
             opt_level_t _opt_level):
             Key_Farm(_win_func, _win_len, _slide_len, _winType, _pardegree, _name, _closing_func, _routing_func, _opt_level, PatternConfig(0, 1, _slide_len, 0, 1, _slide_len), SEQ)
    {}

    /** 
     *  \brief Constructor II
     *  
     *  \param _rich_win_func the rich non-incremental window processing function
     *  \param _win_len window length (in no. of tuples or in time units)
     *  \param _slide_len slide length (in no. of tuples or in time units)
     *  \param _winType window type (count-based CB or time-based TB)
     *  \param _pardegree parallelism degree of the Key_Farm pattern
     *  \param _name string with the unique name of the pattern
     *  \param _closing_func closing function
     *  \param _routing_func function to map the key hashcode onto an identifier starting from zero to pardegree-1
     *  \param _opt_level optimization level used to build the pattern
     */ 
    Key_Farm(rich_win_func_t _rich_win_func,
             uint64_t _win_len,
             uint64_t _slide_len,
             win_type_t _winType,
             size_t _pardegree,
             string _name,
             closing_func_t _closing_func,
             routing_func_t _routing_func,
             opt_level_t _opt_level):
             Key_Farm(_rich_win_func, _win_len, _slide_len, _winType, _pardegree, _name, _closing_func, _routing_func, _opt_level, PatternConfig(0, 1, _slide_len, 0, 1, _slide_len), SEQ)
    {}

    /** 
     *  \brief Constructor III
     *  
     *  \param _winupdate_func the incremental window processing function
     *  \param _win_len window length (in no. of tuples or in time units)
     *  \param _slide_len slide length (in no. of tuples or in time units)
     *  \param _winType window type (count-based CB or time-based TB)
     *  \param _pardegree parallelism degree of the Key_Farm pattern
     *  \param _name string with the unique name of the pattern
     *  \param _closing_func closing function
     *  \param _routing_func function to map the key hashcode onto an identifier starting from zero to pardegree-1
     *  \param _opt_level optimization level used to build the pattern
     */ 
    Key_Farm(winupdate_func_t _winupdate_func,
             uint64_t _win_len,
             uint64_t _slide_len,
             win_type_t _winType,
             size_t _pardegree,
             string _name,
             closing_func_t _closing_func,
             routing_func_t _routing_func,
             opt_level_t _opt_level):
             Key_Farm(_winupdate_func, _win_len, _slide_len, _winType, _pardegree, _name, _closing_func, _routing_func, _opt_level, PatternConfig(0, 1, _slide_len, 0, 1, _slide_len), SEQ)
    {}

    /** 
     *  \brief Constructor IV
     *  
     *  \param _rich_winupdate_func the rich incremental window processing function
     *  \param _win_len window length (in no. of tuples or in time units)
     *  \param _slide_len slide length (in no. of tuples or in time units)
     *  \param _winType window type (count-based CB or time-based TB)
     *  \param _pardegree parallelism degree of the Key_Farm pattern
     *  \param _name string with the unique name of the pattern
     *  \param _closing_func closing function
     *  \param _routing_func function to map the key hashcode onto an identifier starting from zero to pardegree-1
     *  \param _opt_level optimization level used to build the pattern
     */ 
    Key_Farm(rich_winupdate_func_t _rich_winupdate_func,
             uint64_t _win_len,
             uint64_t _slide_len,
             win_type_t _winType,
             size_t _pardegree,
             string _name,
             closing_func_t _closing_func,
             routing_func_t _routing_func,
             opt_level_t _opt_level):
             Key_Farm(_rich_winupdate_func, _win_len, _slide_len, _winType, _pardegree, _name, _closing_func, _routing_func, _opt_level, PatternConfig(0, 1, _slide_len, 0, 1, _slide_len), SEQ)
    {}

    /** 
     *  \brief Constructor V (Nesting with Pane_Farm)
     *  
     *  \param _pf Pane_Farm instance to be replicated within the Key_Farm pattern
     *  \param _win_len window length (in no. of tuples or in time units)
     *  \param _slide_len slide length (in no. of tuples or in time units)
     *  \param _winType window type (count-based CB or time-based TB)
     *  \param _pardegree parallelism degree of the Key_Farm pattern
     *  \param _name string with the unique name of the pattern
     *  \param _closing_func closing function
     *  \param _routing_func function to map the key hashcode onto an identifier starting from zero to pardegree-1
     *  \param _opt_level optimization level used to build the pattern
     */ 
    Key_Farm(const pane_farm_t &_pf,
             uint64_t _win_len,
             uint64_t _slide_len,
             win_type_t _winType,
             size_t _pardegree,
             string _name,
             closing_func_t _closing_func,
             routing_func_t _routing_func,
             opt_level_t _opt_level): hasComplexWorkers(true), opt_level(_opt_level), winType(_winType)
    {
        // type of the Pane_Farm to be created within the Key_Farm pattern
        using panewrap_farm_t = Pane_Farm<tuple_t, result_t, wrapper_in_t>;
        // check the validity of the windowing parameters
        if (_win_len == 0 || _slide_len == 0) {
            cerr << RED << "WindFlow Error: window length or slide cannot be zero" << DEFAULT << endl;
            exit(EXIT_FAILURE);
        }
        // check the validity of the parallelism degree
        if (_pardegree == 0) {
            cerr << RED << "WindFlow Error: parallelism degree cannot be zero" << DEFAULT << endl;
            exit(EXIT_FAILURE);
        }
        // check the compatibility of the windowing parameters
        if (_pf.win_len != _win_len || _pf.slide_len != _slide_len || _pf.winType != _winType) {
            cerr << RED << "WindFlow Error: incompatible windowing parameters" << DEFAULT << endl;
            exit(EXIT_FAILURE);
        }
        // vector of Pane_Farm instances
        vector<ff_node *> w(_pardegree);
        // create the Pane_Farm instances starting from the passed one
        for (size_t i = 0; i < _pardegree; i++) {
            // configuration structure of the Pane_Farm instances
            PatternConfig configPF(0, 1, _slide_len, 0, 1, _slide_len);
            // create the correct Pane_Farm instance
            panewrap_farm_t *pf_W = nullptr;

            if (_pf.isNICPLQ && _pf.isNICWLQ && !_pf.isRichPLQ && !_pf.isRichWLQ)
                pf_W = new panewrap_farm_t(_pf.plq_func, _pf.wlq_func, _pf.win_len, _pf.slide_len, _pf.winType, _pf.plq_degree, _pf.wlq_degree, _name + "_kf_" + to_string(i), _pf.closing_func, false, _pf.opt_level, configPF);
            if (_pf.isNICPLQ && !_pf.isNICWLQ && !_pf.isRichPLQ && !_pf.isRichWLQ)
                pf_W = new panewrap_farm_t(_pf.plq_func, _pf.wlqupdate_func, _pf.win_len, _pf.slide_len, _pf.winType, _pf.plq_degree, _pf.wlq_degree, _name + "_kf_" + to_string(i), _pf.closing_func, false, _pf.opt_level, configPF);
            if (!_pf.isNICPLQ && _pf.isNICWLQ && !_pf.isRichPLQ && !_pf.isRichWLQ)
                pf_W = new panewrap_farm_t(_pf.plqupdate_func, _pf.wlq_func, _pf.win_len, _pf.slide_len, _pf.winType, _pf.plq_degree, _pf.wlq_degree, _name + "_kf_" + to_string(i), _pf.closing_func, false, _pf.opt_level, configPF);
            if (!_pf.isNICPLQ && !_pf.isNICWLQ && !_pf.isRichPLQ && !_pf.isRichWLQ)
                pf_W = new panewrap_farm_t(_pf.plqupdate_func, _pf.wlqupdate_func, _pf.win_len, _pf.slide_len, _pf.winType, _pf.plq_degree, _pf.wlq_degree, _name + "_kf_" + to_string(i), _pf.closing_func, false, _pf.opt_level, configPF);

            if (_pf.isNICPLQ && _pf.isNICWLQ && _pf.isRichPLQ && !_pf.isRichWLQ)
                pf_W = new panewrap_farm_t(_pf.rich_plq_func, _pf.wlq_func, _pf.win_len, _pf.slide_len, _pf.winType, _pf.plq_degree, _pf.wlq_degree, _name + "_kf_" + to_string(i), _pf.closing_func, false, _pf.opt_level, configPF);
            if (_pf.isNICPLQ && !_pf.isNICWLQ && _pf.isRichPLQ && !_pf.isRichWLQ)
                pf_W = new panewrap_farm_t(_pf.rich_plq_func, _pf.wlqupdate_func, _pf.win_len, _pf.slide_len, _pf.winType, _pf.plq_degree, _pf.wlq_degree, _name + "_kf_" + to_string(i), _pf.closing_func, false, _pf.opt_level, configPF);
            if (!_pf.isNICPLQ && _pf.isNICWLQ && _pf.isRichPLQ && !_pf.isRichWLQ)
                pf_W = new panewrap_farm_t(_pf.rich_plqupdate_func, _pf.wlq_func, _pf.win_len, _pf.slide_len, _pf.winType, _pf.plq_degree, _pf.wlq_degree, _name + "_kf_" + to_string(i), _pf.closing_func, false, _pf.opt_level, configPF);
            if (!_pf.isNICPLQ && !_pf.isNICWLQ && _pf.isRichPLQ && !_pf.isRichWLQ)
                pf_W = new panewrap_farm_t(_pf.rich_plqupdate_func, _pf.wlqupdate_func, _pf.win_len, _pf.slide_len, _pf.winType, _pf.plq_degree, _pf.wlq_degree, _name + "_kf_" + to_string(i), _pf.closing_func, false, _pf.opt_level, configPF);

            if (_pf.isNICPLQ && _pf.isNICWLQ && !_pf.isRichPLQ && _pf.isRichWLQ)
                pf_W = new panewrap_farm_t(_pf.plq_func, _pf.rich_wlq_func, _pf.win_len, _pf.slide_len, _pf.winType, _pf.plq_degree, _pf.wlq_degree, _name + "_kf_" + to_string(i), _pf.closing_func, false, _pf.opt_level, configPF);
            if (_pf.isNICPLQ && !_pf.isNICWLQ && !_pf.isRichPLQ && _pf.isRichWLQ)
                pf_W = new panewrap_farm_t(_pf.plq_func, _pf.rich_wlqupdate_func, _pf.win_len, _pf.slide_len, _pf.winType, _pf.plq_degree, _pf.wlq_degree, _name + "_kf_" + to_string(i), _pf.closing_func, false, _pf.opt_level, configPF);
            if (!_pf.isNICPLQ && _pf.isNICWLQ && !_pf.isRichPLQ && _pf.isRichWLQ)
                pf_W = new panewrap_farm_t(_pf.plqupdate_func, _pf.rich_wlq_func, _pf.win_len, _pf.slide_len, _pf.winType, _pf.plq_degree, _pf.wlq_degree, _name + "_kf_" + to_string(i), _pf.closing_func, false, _pf.opt_level, configPF);
            if (!_pf.isNICPLQ && !_pf.isNICWLQ && !_pf.isRichPLQ && _pf.isRichWLQ)
                pf_W = new panewrap_farm_t(_pf.plqupdate_func, _pf.rich_wlqupdate_func, _pf.win_len, _pf.slide_len, _pf.winType, _pf.plq_degree, _pf.wlq_degree, _name + "_kf_" + to_string(i), _pf.closing_func, false, _pf.opt_level, configPF);

            if (_pf.isNICPLQ && _pf.isNICWLQ && _pf.isRichPLQ && _pf.isRichWLQ)
                pf_W = new panewrap_farm_t(_pf.rich_plq_func, _pf.rich_wlq_func, _pf.win_len, _pf.slide_len, _pf.winType, _pf.plq_degree, _pf.wlq_degree, _name + "_kf_" + to_string(i), _pf.closing_func, false, _pf.opt_level, configPF);
            if (_pf.isNICPLQ && !_pf.isNICWLQ && _pf.isRichPLQ && _pf.isRichWLQ)
                pf_W = new panewrap_farm_t(_pf.rich_plq_func, _pf.rich_wlqupdate_func, _pf.win_len, _pf.slide_len, _pf.winType, _pf.plq_degree, _pf.wlq_degree, _name + "_kf_" + to_string(i), _pf.closing_func, false, _pf.opt_level, configPF);
            if (!_pf.isNICPLQ && _pf.isNICWLQ && _pf.isRichPLQ && _pf.isRichWLQ)
                pf_W = new panewrap_farm_t(_pf.rich_plqupdate_func, _pf.rich_wlq_func, _pf.win_len, _pf.slide_len, _pf.winType, _pf.plq_degree, _pf.wlq_degree, _name + "_kf_" + to_string(i), _pf.closing_func, false, _pf.opt_level, configPF);
            if (!_pf.isNICPLQ && !_pf.isNICWLQ && _pf.isRichPLQ && _pf.isRichWLQ)
                pf_W = new panewrap_farm_t(_pf.rich_plqupdate_func, _pf.rich_wlqupdate_func, _pf.win_len, _pf.slide_len, _pf.winType, _pf.plq_degree, _pf.wlq_degree, _name + "_kf_" + to_string(i), _pf.closing_func, false, _pf.opt_level, configPF);

            w[i] = pf_W;
        }
        ff_farm::add_workers(w);
        // create the Emitter and Collector nodes
        ff_farm::add_collector(new kf_collector_t());
        ff_farm::add_emitter(new kf_emitter_t(_routing_func, _pardegree));
        // optimization process according to the provided optimization level
        this->optimize_KeyFarm(_opt_level);
        // when the Key_Farm will be destroyed we need aslo to destroy the emitter, workers and collector
        ff_farm::cleanup_all();
    }

    /** 
     *  \brief Constructor VI (Nesting with Win_MapReduce)
     *  
     *  \param _wm Win_MapReduce instance to be replicated within the Key_Farm pattern
     *  \param _win_len window length (in no. of tuples or in time units)
     *  \param _slide_len slide length (in no. of tuples or in time units)
     *  \param _winType window type (count-based CB or time-based TB)
     *  \param _pardegree parallelism degree of the Key_Farm pattern
     *  \param _name string with the unique name of the pattern
     *  \param _closing_func closing function
     *  \param _routing_func function to map the key hashcode onto an identifier starting from zero to pardegree-1
     *  \param _opt_level optimization level used to build the pattern
     */ 
    Key_Farm(const win_mapreduce_t &_wm,
             uint64_t _win_len,
             uint64_t _slide_len,
             win_type_t _winType,
             size_t _pardegree,
             string _name,
             closing_func_t _closing_func,
             routing_func_t _routing_func,
             opt_level_t _opt_level): hasComplexWorkers(true), opt_level(_opt_level), winType(_winType)
    {
        // type of the Win_MapReduce to be created within the Key_Farm pattern
        using winwrap_map_t = Win_MapReduce<tuple_t, result_t, wrapper_in_t>;
        // check the validity of the windowing parameters
        if (_win_len == 0 || _slide_len == 0) {
            cerr << RED << "WindFlow Error: window length or slide cannot be zero" << DEFAULT << endl;
            exit(EXIT_FAILURE);
        }
        // check the validity of the parallelism degree
        if (_pardegree == 0) {
            cerr << RED << "WindFlow Error: parallelism degree cannot be zero" << DEFAULT << endl;
            exit(EXIT_FAILURE);
        }
        // check the compatibility of the windowing parameters
        if (_wm.win_len != _win_len || _wm.slide_len != _slide_len || _wm.winType != _winType) {
            cerr << RED << "WindFlow Error: incompatible windowing parameters" << DEFAULT << endl;
            exit(EXIT_FAILURE);
        }
        // vector of Win_MapReduce instances
        vector<ff_node *> w(_pardegree);
        // create the Win_MapReduce instances starting from the passed one
        for (size_t i = 0; i < _pardegree; i++) {
            // configuration structure of the Win_MapReduce instances
            PatternConfig configWM(0, 1, _slide_len, 0, 1, _slide_len);
            // create the correct Win_MapReduce instance
            winwrap_map_t *wm_W = nullptr;

            if (_wm.isNICMAP && _wm.isNICREDUCE && !_wm.isRichMAP && !_wm.isRichREDUCE)
                wm_W = new winwrap_map_t(_wm.map_func, _wm.reduce_func, _wm.win_len, _wm.slide_len, _wm.winType, _wm.map_degree, _wm.reduce_degree, _name + "_wf_" + to_string(i), _wm.closing_func, false, _wm.opt_level, configWM);
            if (_wm.isNICMAP && !_wm.isNICREDUCE && !_wm.isRichMAP && !_wm.isRichREDUCE)
                wm_W = new winwrap_map_t(_wm.map_func, _wm.reduceupdate_func, _wm.win_len, _wm.slide_len, _wm.winType, _wm.map_degree, _wm.reduce_degree, _name + "_wf_" + to_string(i), _wm.closing_func, false, _wm.opt_level, configWM);
            if (!_wm.isNICMAP && _wm.isNICREDUCE && !_wm.isRichMAP && !_wm.isRichREDUCE)
                wm_W = new winwrap_map_t(_wm.mapupdate_func, _wm.reduce_func, _wm.win_len, _wm.slide_len, _wm.winType, _wm.map_degree, _wm.reduce_degree, _name + "_wf_" + to_string(i), _wm.closing_func, false, _wm.opt_level, configWM);
            if (!_wm.isNICMAP && !_wm.isNICREDUCE && !_wm.isRichMAP && !_wm.isRichREDUCE)
                wm_W = new winwrap_map_t(_wm.mapupdate_func, _wm.reduceupdate_func, _wm.win_len, _wm.slide_len, _wm.winType, _wm.map_degree, _wm.reduce_degree, _name + "_wf_" + to_string(i), _wm.closing_func, false, _wm.opt_level, configWM);

            if (_wm.isNICMAP && _wm.isNICREDUCE && _wm.isRichMAP && !_wm.isRichREDUCE)
                wm_W = new winwrap_map_t(_wm.rich_map_func, _wm.reduce_func, _wm.win_len, _wm.slide_len, _wm.winType, _wm.map_degree, _wm.reduce_degree, _name + "_wf_" + to_string(i), _wm.closing_func, false, _wm.opt_level, configWM);
            if (_wm.isNICMAP && !_wm.isNICREDUCE && _wm.isRichMAP && !_wm.isRichREDUCE)
                wm_W = new winwrap_map_t(_wm.rich_map_func, _wm.reduceupdate_func, _wm.win_len, _wm.slide_len, _wm.winType, _wm.map_degree, _wm.reduce_degree, _name + "_wf_" + to_string(i), _wm.closing_func, false, _wm.opt_level, configWM);
            if (!_wm.isNICMAP && _wm.isNICREDUCE && _wm.isRichMAP && !_wm.isRichREDUCE)
                wm_W = new winwrap_map_t(_wm.rich_mapupdate_func, _wm.reduce_func, _wm.win_len, _wm.slide_len, _wm.winType, _wm.map_degree, _wm.reduce_degree, _name + "_wf_" + to_string(i), _wm.closing_func, false, _wm.opt_level, configWM);
            if (!_wm.isNICMAP && !_wm.isNICREDUCE && _wm.isRichMAP && !_wm.isRichREDUCE)
                wm_W = new winwrap_map_t(_wm.rich_mapupdate_func, _wm.reduceupdate_func, _wm.win_len, _wm.slide_len, _wm.winType, _wm.map_degree, _wm.reduce_degree, _name + "_wf_" + to_string(i), _wm.closing_func, false, _wm.opt_level, configWM);

            if (_wm.isNICMAP && _wm.isNICREDUCE && !_wm.isRichMAP && _wm.isRichREDUCE)
                wm_W = new winwrap_map_t(_wm.map_func, _wm.rich_reduce_func, _wm.win_len, _wm.slide_len, _wm.winType, _wm.map_degree, _wm.reduce_degree, _name + "_wf_" + to_string(i), _wm.closing_func, false, _wm.opt_level, configWM);
            if (_wm.isNICMAP && !_wm.isNICREDUCE && !_wm.isRichMAP && _wm.isRichREDUCE)
                wm_W = new winwrap_map_t(_wm.map_func, _wm.rich_reduceupdate_func, _wm.win_len, _wm.slide_len, _wm.winType, _wm.map_degree, _wm.reduce_degree, _name + "_wf_" + to_string(i), _wm.closing_func, false, _wm.opt_level, configWM);
            if (!_wm.isNICMAP && _wm.isNICREDUCE && !_wm.isRichMAP && _wm.isRichREDUCE)
                wm_W = new winwrap_map_t(_wm.mapupdate_func, _wm.rich_reduce_func, _wm.win_len, _wm.slide_len, _wm.winType, _wm.map_degree, _wm.reduce_degree, _name + "_wf_" + to_string(i), _wm.closing_func, false, _wm.opt_level, configWM);
            if (!_wm.isNICMAP && !_wm.isNICREDUCE && !_wm.isRichMAP && _wm.isRichREDUCE)
                wm_W = new winwrap_map_t(_wm.mapupdate_func, _wm.rich_reduceupdate_func, _wm.win_len, _wm.slide_len, _wm.winType, _wm.map_degree, _wm.reduce_degree, _name + "_wf_" + to_string(i), _wm.closing_func, false, _wm.opt_level, configWM);

            if (_wm.isNICMAP && _wm.isNICREDUCE && _wm.isRichMAP && _wm.isRichREDUCE)
                wm_W = new winwrap_map_t(_wm.rich_map_func, _wm.rich_reduce_func, _wm.win_len, _wm.slide_len, _wm.winType, _wm.map_degree, _wm.reduce_degree, _name + "_wf_" + to_string(i), _wm.closing_func, false, _wm.opt_level, configWM);
            if (_wm.isNICMAP && !_wm.isNICREDUCE && _wm.isRichMAP && _wm.isRichREDUCE)
                wm_W = new winwrap_map_t(_wm.rich_map_func, _wm.rich_reduceupdate_func, _wm.win_len, _wm.slide_len, _wm.winType, _wm.map_degree, _wm.reduce_degree, _name + "_wf_" + to_string(i), _wm.closing_func, false, _wm.opt_level, configWM);
            if (!_wm.isNICMAP && _wm.isNICREDUCE && _wm.isRichMAP && _wm.isRichREDUCE)
                wm_W = new winwrap_map_t(_wm.rich_mapupdate_func, _wm.rich_reduce_func, _wm.win_len, _wm.slide_len, _wm.winType, _wm.map_degree, _wm.reduce_degree, _name + "_wf_" + to_string(i), _wm.closing_func, false, _wm.opt_level, configWM);
            if (!_wm.isNICMAP && !_wm.isNICREDUCE && _wm.isRichMAP && _wm.isRichREDUCE)
                wm_W = new winwrap_map_t(_wm.rich_mapupdate_func, _wm.rich_reduceupdate_func, _wm.win_len, _wm.slide_len, _wm.winType, _wm.map_degree, _wm.reduce_degree, _name + "_wf_" + to_string(i), _wm.closing_func, false, _wm.opt_level, configWM);

            w[i] = wm_W;
        }
        ff_farm::add_workers(w);
        // create the Emitter and Collector nodes
        ff_farm::add_collector(new kf_collector_t());
        ff_farm::add_emitter(new kf_emitter_t(_routing_func, _pardegree));
        // optimization process according to the provided optimization level
        this->optimize_KeyFarm(_opt_level);
        // when the Key_Farm will be destroyed we need aslo to destroy the emitter, workers and collector
        ff_farm::cleanup_all();
    }

    /** 
     *  \brief Check whether the Win_Farm has been instantiated with complex patterns inside
     *  \return true if the Win_Farm has complex patterns inside
     */
    bool useComplexNesting() { return hasComplexWorkers; }

    /** 
     *  \brief Get the optimization level used to build the pattern
     *  \return adopted utilization level by the pattern
     */
    opt_level_t getOptLevel() { return opt_level; }

    /** 
     *  \brief Get the window type (CB or TB) utilized by the pattern
     *  \return adopted windowing semantics (count- or time-based)
     */
    win_type_t getWinType() { return winType; }
};

#endif
