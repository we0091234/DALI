// Copyright (c) 2017, NVIDIA CORPORATION. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef DALI_PIPELINE_OPERATORS_READER_LOADER_LMDB_H_
#define DALI_PIPELINE_OPERATORS_READER_LOADER_LMDB_H_

#include <lmdb.h>
#include <memory>
#include <string>

#include "dali/core/common.h"
#include "dali/pipeline/operators/reader/loader/loader.h"

namespace dali {

#define CHECK_LMDB(status, filename) \
  do { \
    DALI_ENFORCE(status == MDB_SUCCESS, "LMDB Error: " + string(mdb_strerror(status)) + \
                                        ", with file: " + filename); \
  } while (0)

namespace lmdb {
  inline bool SeekLMDB(MDB_cursor* cursor, MDB_cursor_op op, MDB_val* key, MDB_val *value,
                       const string &filename) {
    int status = mdb_cursor_get(cursor, key, value, op);

    if (status == MDB_NOTFOUND) {
      // reached the end of the db
      return false;
    } else {
      CHECK_LMDB(status, filename);
      return true;
    }
  }

  inline uint64_t LMDB_size(MDB_txn* txn, MDB_dbi dbi, const string &filename) {
    MDB_stat stat;

    CHECK_LMDB(mdb_stat(txn, dbi, &stat), filename);

    return stat.ms_entries;
  }

  inline void PrintLMDBStats(MDB_txn* txn, MDB_dbi dbi, const string &filename) {
    MDB_stat stat;

    CHECK_LMDB(mdb_stat(txn, dbi, &stat), filename);

    printf("DB has %d entries\n", static_cast<int>(stat.ms_entries));
  }
}  // namespace lmdb

class LMDBLoader : public Loader<CPUBackend, Tensor<CPUBackend>> {
 public:
  explicit LMDBLoader(const OpSpec& options)
    : Loader(options),
      db_path_(options.GetArgument<string>("path")) {
  }

  ~LMDBLoader() override {
    if (mdb_cursor_) {
      mdb_cursor_close(mdb_cursor_);
      mdb_dbi_close(mdb_env_, mdb_dbi_);
    }
    if (mdb_transaction_)
      mdb_txn_abort(mdb_transaction_);
    if (mdb_env_)
      mdb_env_close(mdb_env_);
    mdb_env_ = nullptr;
  }

  void ReadSample(Tensor<CPUBackend>& tensor) override {
    // assume cursor is valid, read next, loop to start if necessary
    lmdb::SeekLMDB(mdb_cursor_, MDB_NEXT, &key_, &value_, db_path_);
    ++current_index_;

    MoveToNextShard(current_index_);

    std::string image_key =
      db_path_ + " at key " + to_string(reinterpret_cast<char*>(key_.mv_data));
    DALIMeta meta;

    meta.SetSourceInfo(image_key);
    meta.SetSkipSample(false);

    tensor.set_type(TypeInfo::Create<uint8_t>());

    // if image is cached, skip loading
    if (ShouldSkipImage(image_key)) {
      meta.SetSkipSample(true);
      tensor.Reset();
      tensor.SetMeta(meta);
      tensor.set_type(TypeInfo::Create<uint8_t>());
      tensor.Resize({0});
      return;
    }

    tensor.SetMeta(meta);
    tensor.Resize({static_cast<Index>(value_.mv_size)});
    std::memcpy(tensor.raw_mutable_data(),
                reinterpret_cast<uint8_t*>(value_.mv_data),
                value_.mv_size*sizeof(uint8_t));
  }

 protected:
  Index SizeImpl() override {
    return lmdb_size_;
  }

  void PrepareMetadataImpl() override {
    // Create the db environment, open the passed DB
    CHECK_LMDB(mdb_env_create(&mdb_env_), db_path_);
    auto mdb_flags = MDB_RDONLY | MDB_NOTLS | MDB_NOLOCK;
    CHECK_LMDB(mdb_env_open(mdb_env_, db_path_.c_str(), mdb_flags, 0664), db_path_);

    // Create transaction and cursor
    CHECK_LMDB(mdb_txn_begin(mdb_env_, NULL, MDB_RDONLY, &mdb_transaction_), db_path_);
    CHECK_LMDB(mdb_dbi_open(mdb_transaction_, NULL, 0, &mdb_dbi_), db_path_);
    CHECK_LMDB(mdb_cursor_open(mdb_transaction_, mdb_dbi_, &mdb_cursor_), db_path_);
    lmdb_size_ = lmdb::LMDB_size(mdb_transaction_, mdb_dbi_, db_path_);

    // Optional: debug printing
    lmdb::PrintLMDBStats(mdb_transaction_, mdb_dbi_, db_path_);

    Reset(true);
  }

 private:
  void Reset(bool wrap_to_shard) override {
    // work out how many entries to move forward to handle sharding
    current_index_ = start_index(shard_id_, num_shards_, Size());
    bool ok = lmdb::SeekLMDB(mdb_cursor_, MDB_FIRST, &key_, &value_, db_path_);
    DALI_ENFORCE(ok, "lmdb::SeekLMDB to the beginning failed");

    if (wrap_to_shard) {
      for (size_t i = 0; i < current_index_; ++i) {
        bool ok = lmdb::SeekLMDB(mdb_cursor_, MDB_NEXT, &key_, &value_, db_path_);
        DALI_ENFORCE(ok, "lmdb::SeekLMDB to position " + to_string(current_index_) + " failed");
      }
    }
  }
  using Loader<CPUBackend, Tensor<CPUBackend>>::shard_id_;
  using Loader<CPUBackend, Tensor<CPUBackend>>::num_shards_;

  MDB_env* mdb_env_ = nullptr;
  MDB_cursor* mdb_cursor_ = nullptr;
  MDB_dbi mdb_dbi_;
  MDB_txn* mdb_transaction_ = nullptr;
  size_t current_index_;
  Index lmdb_size_;

  // values
  MDB_val key_, value_;

  // options
  string db_path_;
};

};  // namespace dali

#endif  // DALI_PIPELINE_OPERATORS_READER_LOADER_LMDB_H_
