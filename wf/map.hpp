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
 *  @file    map.hpp
 *  @author  Gabriele Mencagli
 *  @date    08/01/2019
 *  
 *  @brief Map operator executing a one-to-one transformation on the input stream
 *  
 *  @section Map (Description)
 *  
 *  This file implements the Map operator able to execute a one-to-one transformation
 *  on each tuple of the input data stream.
 *  
 *  The template parameters tuple_t and result_t must be default constructible, with a
 *  copy Constructor and a copy assignment operator, and they must provide and implement
 *  the setControlFields() and getControlFields() methods.
 */ 

#ifndef MAP_H
#define MAP_H

/// includes
#include<string>
#include<ff/node.hpp>
#include<ff/pipeline.hpp>
#include<ff/multinode.hpp>
#include<ff/farm.hpp>
#include<basic.hpp>
#include<context.hpp>
#if defined (TRACE_WINDFLOW)
    #include<stats_record.hpp>
#endif
#include<basic_operator.hpp>
#include<standard_emitter.hpp>

namespace wf {

/** 
 *  \class Map
 *  
 *  \brief Map operator executing a one-to-one transformation on the input stream
 *  
 *  This class implements the Map operator executing a one-to-one transformation
 *  on each tuple of the input stream.
 */ 
template<typename tuple_t, typename result_t>
class Map: public ff::ff_farm, public Basic_Operator
{
public:
    /// type of the map function (in-place version)
    using map_func_ip_t = std::function<void(tuple_t &)>;
    /// type of the rich map function (in-place version)
    using rich_map_func_ip_t = std::function<void(tuple_t &, RuntimeContext &)>;
    /// type of the map function (not in-place version)
    using map_func_nip_t = std::function<void(const tuple_t &, result_t &)>;
    /// type of the rich map function (not in-place version)
    using rich_map_func_nip_t = std::function<void(const tuple_t &, result_t &, RuntimeContext &)>;
    /// type of the closing function
    using closing_func_t = std::function<void(RuntimeContext &)>;
    /// type of the function to map the key hashcode onto an identifier starting from zero to parallelism-1
    using routing_func_t = std::function<size_t(size_t, size_t)>;

private:
    // friendships with other classes in the library
    friend class MultiPipe;
    std::string name; // name of the Map
    size_t parallelism; // internal parallelism of the Map
    bool keyed; // flag stating whether the Map is configured with keyBy or not
    bool used; // true if the Map has been added/chained in a MultiPipe
    // class Map_Node
    class Map_Node: public ff::ff_minode_t<tuple_t, result_t>
    {
private:
        map_func_ip_t func_ip; // in-place map function
        rich_map_func_ip_t rich_func_ip; // in-place rich map function
        map_func_nip_t func_nip; // not in-place map function
        rich_map_func_nip_t rich_func_nip; // not in-place rich map function
        closing_func_t closing_func; // closing function
        std::string name; // string of the unique name of the operator
        bool isIP; // flag stating if the in-place map function should be used (otherwise the not in-place version)
        bool isRich; // flag stating whether the function to be used is rich (i.e. it receives the RuntimeContext object)
        RuntimeContext context; // RuntimeContext
        size_t eos_received; // number of received EOS messages
        bool terminated; // true if the replica has finished its work
#if defined (TRACE_WINDFLOW)
        Stats_Record stats_record;
        double avg_td_us = 0;
        double avg_ts_us = 0;
        volatile uint64_t startTD, startTS, endTD, endTS;
#endif

public:
        // Constructor I
        template<typename T=std::string>
        Map_Node(typename std::enable_if<std::is_same<T,T>::value && std::is_same<tuple_t,result_t>::value, map_func_ip_t>::type _func,
                 T _name,
                 RuntimeContext _context,
                 closing_func_t _closing_func):
                 func_ip(_func),
                 closing_func(_closing_func),
                 name(_name),
                 isIP(true),
                 isRich(false),
                 context(_context),
                 eos_received(0),
                 terminated(false) {}

        // Constructor II
        template<typename T=std::string>
        Map_Node(typename std::enable_if<std::is_same<T,T>::value && std::is_same<tuple_t,result_t>::value, rich_map_func_ip_t>::type _func,
                 T _name,
                 RuntimeContext _context,
                 closing_func_t _closing_func):
                 rich_func_ip(_func),
                 closing_func(_closing_func),
                 name(_name),
                 isIP(true),
                 isRich(true),
                 context(_context),
                 eos_received(0),
                 terminated(false) {}

        // Constructor III
        template<typename T=std::string>
        Map_Node(typename std::enable_if<std::is_same<T,T>::value && !std::is_same<tuple_t,result_t>::value, map_func_nip_t>::type _func,
                 T _name,
                 RuntimeContext _context,
                 closing_func_t _closing_func):
                 func_nip(_func),
                 closing_func(_closing_func),
                 name(_name),
                 isIP(false),
                 isRich(false),
                 context(_context),
                 eos_received(0),
                 terminated(false) {}

        // Constructor IV
        template<typename T=std::string>
        Map_Node(typename std::enable_if<std::is_same<T,T>::value && !std::is_same<tuple_t,result_t>::value, rich_map_func_nip_t>::type _func,
                 T _name,
                 RuntimeContext _context,
                 closing_func_t _closing_func):
                 rich_func_nip(_func),
                 closing_func(_closing_func),
                 name(_name),
                 isIP(false),
                 isRich(true),
                 context(_context),
                 eos_received(0),
                 terminated(false) {}

        // svc_init method (utilized by the FastFlow runtime)
        int svc_init() override
        {
#if defined (TRACE_WINDFLOW)
            stats_record = Stats_Record(name, std::to_string(this->get_my_id()), false, false);
#endif
            return 0;
        }

        // svc method (utilized by the FastFlow runtime)
        result_t *svc(tuple_t *t) override
        {
#if defined (TRACE_WINDFLOW)
            startTS = current_time_nsecs();
            if (stats_record.inputs_received == 0) {
                startTD = current_time_nsecs();
            }
            stats_record.inputs_received++;
            stats_record.bytes_received += sizeof(tuple_t);
            stats_record.outputs_sent++;
            stats_record.bytes_sent += sizeof(result_t);
#endif
            result_t *r;
            // in-place version
            if (isIP) {
                if (!isRich) {
                    func_ip(*t);
                }
                else {
                    rich_func_ip(*t, context);
                }
                r = reinterpret_cast<result_t *>(t);
            }
            else {
                r = new result_t();
                if (!isRich) {
                    func_nip(*t, *r);
                }
                else {
                    rich_func_nip(*t, *r, context);
                }
                delete t;
            }
#if defined (TRACE_WINDFLOW)
            endTS = current_time_nsecs();
            endTD = current_time_nsecs();
            double elapsedTS_us = ((double) (endTS - startTS)) / 1000;
            avg_ts_us += (1.0 / stats_record.inputs_received) * (elapsedTS_us - avg_ts_us);
            double elapsedTD_us = ((double) (endTD - startTD)) / 1000;
            avg_td_us += (1.0 / stats_record.inputs_received) * (elapsedTD_us - avg_td_us);
            stats_record.service_time = std::chrono::duration<double, std::micro>(avg_ts_us);
            stats_record.eff_service_time = std::chrono::duration<double, std::micro>(avg_td_us);
            startTD = current_time_nsecs();
#endif
            return r;
        }

        // method to manage the EOS (utilized by the FastFlow runtime)
        void eosnotify(ssize_t id) override
        {
            eos_received++;
            // check the number of received EOS messages
            if ((eos_received != this->get_num_inchannels()) && (this->get_num_inchannels() != 0)) { // workaround due to FastFlow
                return;
            }
            terminated = true;
#if defined (TRACE_WINDFLOW)
            stats_record.set_Terminated();
#endif
        }

        // svc_end method (utilized by the FastFlow runtime)
        void svc_end() override
        {
            // call the closing function
            closing_func(context);
        }

        // method the check the termination of the replica
        bool isTerminated() const
        {
            return terminated;
        }

#if defined (TRACE_WINDFLOW)
        // method to return a copy of the Stats_Record of this node
        Stats_Record get_StatsRecord() const
        {
            return stats_record;
        }
#endif
    };

public:
    /** 
     *  \brief Constructor I
     *  
     *  \param _func function with signature accepted by the Map operator
     *  \param _parallelism internal parallelism of the Map operator
     *  \param _name name of the Map operator
     *  \param _closing_func closing function
     */ 
    template<typename F_t>
    Map(F_t _func,
        size_t _parallelism,
        std::string _name, 
        closing_func_t _closing_func):
        name(_name),
        parallelism(_parallelism),
        keyed(false),
        used(false)
    {
        // check the validity of the parallelism value
        if (_parallelism == 0) {
            std::cerr << RED << "WindFlow Error: Map has parallelism zero" << DEFAULT_COLOR << std::endl;
            exit(EXIT_FAILURE);
        }
        // vector of Map_Node
        std::vector<ff_node *> w;
        for (size_t i=0; i<_parallelism; i++) {
            auto *seq = new Map_Node(_func, _name, RuntimeContext(_parallelism, i), _closing_func);
            w.push_back(seq);
        }
        // add emitter
        ff::ff_farm::add_emitter(new Standard_Emitter<tuple_t>(_parallelism));
        // add workers
        ff::ff_farm::add_workers(w);
        // add default collector
        ff::ff_farm::add_collector(nullptr);
        // when the Map will be destroyed we need aslo to destroy the emitter, workers and collector
        ff::ff_farm::cleanup_all();
    }

    /** 
     *  \brief Constructor II
     *  
     *  \param _func function with signature accepted by the Map operator
     *  \param _parallelism internal parallelism of the Map operator
     *  \param _name name of the Map operator
     *  \param _closing_func closing function
     *  \param _routing_func function to map the key hashcode onto an identifier starting from zero to parallelism-1
     */ 
    template<typename F_t>
    Map(F_t _func,
        size_t _parallelism,
        std::string _name,
        closing_func_t _closing_func, 
        routing_func_t _routing_func):
        name(_name),
        parallelism(_parallelism),
        keyed(true),
        used(false)
    {
        // check the validity of the parallelism value
        if (_parallelism == 0) {
            std::cerr << RED << "WindFlow Error: Map has parallelism zero" << DEFAULT_COLOR << std::endl;
            exit(EXIT_FAILURE);
        }
        // vector of Map_Node
        std::vector<ff_node *> w;
        for (size_t i=0; i<_parallelism; i++) {
            auto *seq = new Map_Node(_func, _name, RuntimeContext(_parallelism, i), _closing_func);
            w.push_back(seq);
        }
        // add emitter
        ff::ff_farm::add_emitter(new Standard_Emitter<tuple_t>(_routing_func, _parallelism));
        // add workers
        ff::ff_farm::add_workers(w);
        // add default collector
        ff::ff_farm::add_collector(nullptr);
        // when the Map will be destroyed we need aslo to destroy the emitter, workers and collector
        ff::ff_farm::cleanup_all();
    }

    /** 
     *  \brief Get the name of the Map
     *  \return name of the Map
     */ 
    std::string getName() const override
    {
        return name;
    }

    /** 
     *  \brief Get the total parallelism within the Map
     *  \return total parallelism within the Map
     */ 
    size_t getParallelism() const override
    {
        return parallelism;
    }

    /** 
     *  \brief Return the routing mode of inputs to the Map
     *  \return routing mode used by the Map
     */ 
    routing_modes_t getRoutingMode() const override
    {
        if (keyed) {
            return routing_modes_t::KEYBY;
        }
        else {
            return routing_modes_t::FORWARD;
        }
    }

    /** 
     *  \brief Check whether the Map has been used in a MultiPipe
     *  \return true if the Map has been added/chained to an existing MultiPipe
     */ 
    bool isUsed() const override
    {
        return used;
    }

    /** 
     *  \brief Check whether the operator has been terminated
     *  \return true if the operator has finished its work
     */ 
    virtual bool isTerminated() const override
    {
        bool terminated = true;
        // scan all the replicas to check their termination
        for(auto *w: this->getWorkers()) {
            auto *node = static_cast<Map_Node *>(w);
            terminated = terminated && node->isTerminated(); 
        }
        return terminated;
    }

#if defined (TRACE_WINDFLOW)
    /// Dump the log file (JSON format) in the LOG_DIR directory
    void dump_LogFile() const override
    {
        // create and open the log file in the LOG_DIR directory
        std::ofstream logfile;
#if defined (LOG_DIR)
        std::string log_dir = std::string(STRINGIFY(LOG_DIR));
        std::string filename = std::string(STRINGIFY(LOG_DIR)) + "/" + std::to_string(getpid()) + "_" + name + ".json";
#else
        std::string log_dir = std::string("log");
        std::string filename = "log/" + std::to_string(getpid()) + "_" + name + ".json";
#endif
        // create the log directory
        if (mkdir(log_dir.c_str(), 0777) != 0) {
            struct stat st;
            if((stat(log_dir.c_str(), &st) != 0) || !S_ISDIR(st.st_mode)) {
                std::cerr << RED << "WindFlow Error: directory for log files cannot be created" << DEFAULT_COLOR << std::endl;
                exit(EXIT_FAILURE);
            }
        }
        logfile.open(filename);
        // create the rapidjson writer
        rapidjson::StringBuffer buffer;
        rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
        // append the statistics of this operator
        this->append_Stats(writer);
        // serialize the object to file
        logfile << buffer.GetString();
        logfile.close();
    }

    /// append the statistics (JSON format) of this operator
    void append_Stats(rapidjson::PrettyWriter<rapidjson::StringBuffer> &writer) const override
    {
        // create the header of the JSON file
        writer.StartObject();
        writer.Key("Operator_name");
        writer.String(name.c_str());
        writer.Key("Operator_type");
        writer.String("Map");
        writer.Key("Distribution");
        writer.String(keyed ? "KEYBY" : "FORWARD");
        writer.Key("isTerminated");
        writer.Bool(this->isTerminated());
        writer.Key("isWindowed");
        writer.Bool(false);
        writer.Key("isGPU");
        writer.Bool(false);
        writer.Key("Parallelism");
        writer.Uint(parallelism);
        writer.Key("Replicas");
        writer.StartArray();
        // get statistics from all the replicas of the operator
        for(auto *w: this->getWorkers()) {
            auto *node = static_cast<Map_Node *>(w);
            Stats_Record record = node->get_StatsRecord();
            record.append_Stats(writer);
        }
        writer.EndArray();
        writer.EndObject();
    }
#endif

    /// deleted constructors/operators
    Map(const Map &) = delete; // copy constructor
    Map(Map &&) = delete; // move constructor
    Map &operator=(const Map &) = delete; // copy assignment operator
    Map &operator=(Map &&) = delete; // move assignment operator
};

} // namespace wf

#endif
