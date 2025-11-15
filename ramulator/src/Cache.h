#ifndef __CACHE_H
#define __CACHE_H

#include "Config.h"
#include "Request.h"
#include "Statistics.h"
#include <algorithm>
#include <cstdio>
#include <cassert>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <queue>
#include <list>
#include <cstdint> // for uint32_t
#include <sstream>
#include <algorithm>
#include <cctype>

namespace ramulator
{
class CacheSystem;

class Cache {
protected:
  ScalarStat cache_read_miss;
  ScalarStat cache_write_miss;
  ScalarStat cache_total_miss;
  ScalarStat cache_eviction;
  ScalarStat cache_read_access;
  ScalarStat cache_write_access;
  ScalarStat cache_total_access;
  ScalarStat cache_mshr_hit;
  ScalarStat cache_mshr_unavailable;
  ScalarStat cache_set_unavailable;
public:
  enum class Level {
    L1,
    L2,
    L3,
    MAX
  } level;
  std::string level_string;

  struct Line {
    long addr;
    long tag;
    bool lock; // When the lock is on, the value is not valid yet.
    bool dirty;
    int  way;        // stable way index within the set [0..assoc-1]
    int  owner_core; // core that allocated this line (for debugging/metrics)
    Line(long addr, long tag):
        addr(addr), tag(tag), lock(true), dirty(false), way(-1), owner_core(-1) {}
    Line(long addr, long tag, bool lock, bool dirty, int way = -1, int owner_core = -1):
        addr(addr), tag(tag), lock(lock), dirty(dirty), way(way), owner_core(owner_core) {}
  };

  Cache(int size, int assoc, int block_size, int mshr_entry_num,
      Level level, std::shared_ptr<CacheSystem> cachesys);

  void tick();

  // L1, L2, L3 accumulated latencies
  int latency[int(Level::MAX)] = {4, 4 + 12, 4 + 12 + 31};
  int latency_each[int(Level::MAX)] = {4, 12, 31};

  std::shared_ptr<CacheSystem> cachesys;
  // LLC has multiple higher caches
  std::vector<Cache*> higher_cache;
  Cache* lower_cache;

  bool send(Request req);

  void concatlower(Cache* lower);

  void callback(Request& req);

protected:

  bool is_first_level;
  bool is_last_level;
  size_t size;
  unsigned int assoc;
  unsigned int block_num;
  unsigned int index_mask;
  unsigned int block_size;
  unsigned int index_offset;
  unsigned int tag_offset;
  unsigned int mshr_entry_num;
  std::vector<std::pair<long, std::list<Line>::iterator>> mshr_entries;
  std::list<Request> retry_list;

  std::map<int, std::list<Line> > cache_lines;

  int calc_log2(int val) {
      int n = 0;
      while ((val >>= 1))
          n ++;
      return n;
  }

  int get_index(long addr) {
    return (addr >> index_offset) & index_mask;
  };

  long get_tag(long addr) {
    return (addr >> tag_offset);
  }

  // Align the address to cache line size
  long align(long addr) {
    return (addr & ~(block_size-1l));
  }

  // Evict the cache line from higher level to this level.
  // Pass the dirty bit and update LRU queue.
  void evictline(long addr, bool dirty);

  // Invalidate the line from this level to higher levels
  // The return value is a pair. The first element is invalidation
  // latency, and the second is wether the value has new version
  // in higher level and this level.
  std::pair<long, bool> invalidate(long addr);

  // Evict the victim from current set of lines.
  // First do invalidation, then call evictline(L1 or L2) or send
  // a write request to memory(L3) when dirty bit is on.
  void evict(std::list<Line>* lines,
      std::list<Line>::iterator victim);

  // First test whether need eviction, if so, do eviction by
  // calling evict function. Then allocate a new line and return
  // the iterator points to it.
  std::list<Line>::iterator allocate_line(
      std::list<Line>& lines, long addr);

  // Way-partitioned allocation (LLC only): pick a free allowed way or evict
  // an allowed LRU victim. Returns lines.end() to stall if no candidate.
  std::list<Line>::iterator allocate_line_wp(
      std::list<Line>& lines, long addr, int coreid);

  // Helper: test if 'way' is allowed for 'coreid' by mask carried in CacheSystem.
  inline bool way_allowed(int coreid, int way) const;

  // Check whether the set to hold addr has space or eviction is
  // needed.
  bool need_eviction(const std::list<Line>& lines, long addr);

  // Check whether this addr is hit and fill in the pos_ptr with
  // the iterator to the hit line or lines.end()
  bool is_hit(std::list<Line>& lines, long addr,
              std::list<Line>::iterator* pos_ptr);

  bool all_sets_locked(const std::list<Line>& lines) {
    if (lines.size() < assoc) {
      return false;
    }
    for (const auto& line : lines) {
      if (!line.lock) {
        return false;
      }
    }
    return true;
  }

  bool check_unlock(long addr) {
    auto it = cache_lines.find(get_index(addr));
    if (it == cache_lines.end()) {
      return true;
    } else {
      auto& lines = it->second;
      auto line = find_if(lines.begin(), lines.end(),
          [addr, this](Line l){return (l.tag == get_tag(addr));});
      if (line == lines.end()) {
        return true;
      } else {
        bool check = !line->lock;
        if (!is_first_level) {
          for (auto hc : higher_cache) {
            if (!check) {
              return check;
            }
            check = check && hc->check_unlock(line->addr);
          }
        }
        return check;
      }
    }
  }

  std::vector<std::pair<long, std::list<Line>::iterator>>::iterator
  hit_mshr(long addr) {
    auto mshr_it =
        find_if(mshr_entries.begin(), mshr_entries.end(),
            [addr, this](std::pair<long, std::list<Line>::iterator>
                   mshr_entry) {
              return (align(mshr_entry.first) == align(addr));
            });
    return mshr_it;
  }

  std::list<Line>& get_lines(long addr) {
    if (cache_lines.find(get_index(addr))
        == cache_lines.end()) {
      cache_lines.insert(make_pair(get_index(addr),
          std::list<Line>()));
    }
    return cache_lines[get_index(addr)];
  }

};

class CacheSystem {
public:
  CacheSystem(const Config& configs, std::function<bool(Request)> send_memory):
    send_memory(send_memory) {
      if (configs.has_core_caches()) {
        first_level = Cache::Level::L1;
      } else if (configs.has_l3_cache()) {
        first_level = Cache::Level::L3;
      } else {
        last_level = Cache::Level::MAX; // no cache
      }

      if (configs.has_l3_cache()) {
        last_level = Cache::Level::L3;
      } else if (configs.has_core_caches()) {
        last_level = Cache::Level::L2;
      } else {
        last_level = Cache::Level::MAX; // no cache
      }
  
     // cs433
     if(configs.is_way_partitioning()) {
         cache_qos = Cache_QoS::way_partitioning;
         init_way_masks_from_config(configs); // parse now if provided
         // If not provided, we’ll build an equal split lazily at LLC when assoc is known
     }
     else if(configs.is_custom()) {
         cache_qos = Cache_QoS::custom;
     }
     else {
         cache_qos = Cache_QoS::basic;
     }
    }

  // cs433
  enum class Cache_QoS
  {
      basic,
      way_partitioning,
      custom
  } cache_qos; 

  std::vector<uint64_t> way_masks; // supports up to 64-way LLC

  // Parse masks from config (if present). Accepts any of:
  //   llc_way_masks="0x00ff,0xff00,0x0f0f,0xf0f0"
  //   llc_way_mask_core0="0x00ff", ... _coreN="..."
  //   l3_way_mask_core<i>=...
  void init_way_masks_from_config(const Config& cfg) {
    int n = std::max(1, cfg.get_core_num());
    way_masks.assign(n, 0ULL);

    auto trim = [](std::string s){
      auto wsfront = std::find_if_not(s.begin(), s.end(), [](int c){return std::isspace(c);});
      auto wsback  = std::find_if_not(s.rbegin(), s.rend(), [](int c){return std::isspace(c);}).base();
      return (wsback <= wsfront) ? std::string() : std::string(wsfront, wsback);
    };
    auto parse_one = [&](const std::string& t)->uint64_t{
      std::string x = trim(t);
      if (x.rfind("0x",0)==0 || x.rfind("0X",0)==0) return std::stoull(x, nullptr, 16);
      // auto-detect base like std::stoull with base=0 (hex if 0x, else dec)
      return std::stoull(x, nullptr, 0);
    };

    if (cfg.contains("llc_way_masks")) {
      std::string s = cfg["llc_way_masks"];
      std::stringstream ss(s);
      std::string tok; int i=0;
      while (std::getline(ss, tok, ',') && i<n) way_masks[i++] = parse_one(tok);
      return;
    }
    // Per-core keys
    bool any = false;
    for (int i=0;i<n;i++) {
      std::string k1 = "llc_way_mask_core"+std::to_string(i);
      std::string k2 = "l3_way_mask_core"+std::to_string(i);
      std::string k3 = "way_mask_core"+std::to_string(i);
      if (cfg.contains(k1)) { way_masks[i]=parse_one(cfg[k1]); any=true; continue; }
      if (cfg.contains(k2)) { way_masks[i]=parse_one(cfg[k2]); any=true; continue; }
      if (cfg.contains(k3)) { way_masks[i]=parse_one(cfg[k3]); any=true; continue; }
    }
    (void)any; // if none found, we’ll build defaults later when assoc is known
  }

  // If masks weren’t provided, build an equal contiguous split once we know LLC assoc.
  void ensure_masks_initialized(int assoc) {
    if (way_masks.empty()) return;
    bool all_zero = std::all_of(way_masks.begin(), way_masks.end(),
                                [](uint64_t m){return m==0ULL;});
    if (!all_zero) return;
    int n = (int)way_masks.size();
    int base = assoc / n, rem = assoc % n;
    int start = 0;
    for (int i=0;i<n;i++) {
      int chunk = base + (i < rem ? 1 : 0);
      uint64_t mask = 0ULL;
      for (int w=0; w<chunk; ++w) mask |= (1ULL << (start+w));
      way_masks[i] = mask;
      start += chunk;
    }
  }

  // wait_list contains miss requests with their latencies in
  // cache. When this latency is met, the send_memory function
  // will be called to send the request to the memory system.
  std::list<std::pair<long, Request> > wait_list;

  // hit_list contains hit requests with their latencies in cache.
  // callback function will be called when this latency is met and
  // set the instruction status to ready in processor's window.
  std::list<std::pair<long, Request> > hit_list;

  std::function<bool(Request)> send_memory;

  long clk = 0;
  void tick();

  Cache::Level first_level;
  Cache::Level last_level;
  
};

} // namespace ramulator

#endif /* __CACHE_H */
