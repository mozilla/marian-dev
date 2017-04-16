#pragma once

#include <vector>
#include <unordered_map>

#include "training/config.h"
#include "common/definitions.h"
#include "common/file_stream.h"

namespace marian {
  
class FilterInfo {
  private:
    std::vector<Word> indeces_;
    std::vector<Word> mappedIndeces_;
    std::unordered_map<Word, Word> reverseMap_;
    
  public:
    FilterInfo(const std::vector<Word>& indeces,
               const std::vector<Word>& mappedIndeces,
               const std::unordered_map<Word, Word>& reverseMap)
    : indeces_(indeces),
      mappedIndeces_(mappedIndeces),
      reverseMap_(reverseMap) { }
    
    std::vector<Word>& indeces() { return indeces_; }
    std::vector<Word>& mappedIndeces() { return mappedIndeces_; }
    Word reverseMap(Word idx) { return reverseMap_[idx]; }
};
  
class Filter {
  private:
    Ptr<Config> options_;
    Ptr<Vocab> srcVocab_;
    Ptr<Vocab> trgVocab_;
    
    size_t firstNum_{100};
    size_t bestNum_{100};
    
    std::vector<std::unordered_map<size_t, float>> data_;
    
    void load(const std::string& fname) {
      InputFileStream in(fname);
      
      std::string src, trg;
      float prob;
      while(in >> trg >> src >> prob) {
        if(src == "NULL" || trg == "NULL")
          continue;
        
        Word sId = (*srcVocab_)[src];
        Word tId = (*trgVocab_)[trg];
        
        if(data_.size() <= sId)
          data_.resize(sId + 1);
        data_[sId][tId] = prob;
      }
    }
    
    void prune(float threshold=0.f) {
      size_t i = 0;
      for(auto& probs : data_) {
        std::vector<std::pair<float, Word>> sorter;
        for(auto& it : probs)
          sorter.emplace_back(it.second, it.first);
        
        std::sort(sorter.begin(), sorter.end(),
                  std::greater<std::pair<float, Word>>());
        
        probs.clear();
        for(auto& it: sorter) {
          if(probs.size() < bestNum_ && it.first > threshold)
            probs[it.second] = it.first;
          else
            break;
        }
        
        ++i;
      }
    }
    
  public:
    
    Filter(Ptr<Config> options,
           Ptr<Vocab> srcVocab,
           Ptr<Vocab> trgVocab)
    : options_(options),
      srcVocab_(srcVocab), trgVocab_(trgVocab)
    {
      std::vector<std::string> vals
        = options_->get<std::vector<std::string>>("filter");
      
      UTIL_THROW_IF2(vals.empty(), "No path to filter path given");
      std::string fname = vals[0];
      
      firstNum_ = vals.size() > 1 ? std::stoi(vals[1]) : 100;
      bestNum_ = vals.size() > 2 ? std::stoi(vals[2]) : 100;
      float threshold = vals.size() > 3 ? std::stof(vals[3]) : 0;
      
      load(fname);
      prune(threshold);
    }
    
    Ptr<FilterInfo> createInfo(Ptr<data::SubBatch> srcBatch,
                               Ptr<data::SubBatch> trgBatch) {
      
      // add firstNum most frequent words
      std::unordered_set<Word> idxSet;
      for(Word i = 0; i < firstNum_ && i < trgVocab_->size(); ++i)
        idxSet.insert(i);
      
      // add all words from ground truth
      for(auto i : trgBatch->indeces())
        idxSet.insert(i);
      
      // collect unique words form source
      std::unordered_set<Word> srcSet;
      for(auto i : srcBatch->indeces())
        srcSet.insert(i);
      
      // add aligned target words
      for(auto i : srcSet)
        for(auto& it : data_[i])
          idxSet.insert(it.first);
      
      // turn into vector and sort (slected indeces)
      std::vector<Word> idx(idxSet.begin(), idxSet.end());
      std::sort(idx.begin(), idx.end());
      
      // assign new shifted position
      std::unordered_map<Word, Word> pos;
      for(Word i = 0; i < idx.size(); ++i)
        pos[idx[i]] = i;
      
      std::vector<Word> mapped;
      std::unordered_map<Word, Word> reverseMap;
      for(auto i : trgBatch->indeces()) {
        // mapped postions for cross-entropy
        mapped.push_back(pos[i]);
        // reverse mapping to original indeces
        reverseMap[pos[i]] = i;
      }
      
      return New<FilterInfo>(idx, mapped, reverseMap);
    }
};

}