// Protocol Buffers - Google's data interchange format
// Copyright 2023 Google LLC.  All rights reserved.
// https://developers.google.com/protocol-buffers/
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google LLC nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#ifndef UPB_UTIL_DEF_TO_PROTO_TEST_H_
#define UPB_UTIL_DEF_TO_PROTO_TEST_H_

#include <string>

#include "google/protobuf/descriptor.pb.h"
#include "google/protobuf/descriptor.upb.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include "google/protobuf/descriptor.h"
#include "google/protobuf/dynamic_message.h"
#include "google/protobuf/util/field_comparator.h"
#include "upb/upb/base/status.hpp"
#include "upb/upb/mem/arena.hpp"
#include "upb/upb/reflection/def.hpp"
#include "upb/upb/util/def_to_proto.h"

namespace upb_test {

// A gtest matcher that verifies that a proto is equal to `proto`.  Both `proto`
// and `arg` must be messages of type `msgdef_func` (a .upbdefs.h function that
// loads a known msgdef into the given defpool).
MATCHER_P(EqualsProtoTreatNansAsEqual, proto,
          negation ? "are not equal" : "are equal") {
  upb::DefPool defpool;
  google::protobuf::DescriptorPool pool;
  google::protobuf::DynamicMessageFactory factory;
  std::string differences;
  google::protobuf::util::DefaultFieldComparator comparator;
  comparator.set_treat_nan_as_equal(true);
  google::protobuf::util::MessageDifferencer differencer;
  differencer.set_field_comparator(&comparator);
  differencer.ReportDifferencesToString(&differences);
  bool eq = differencer.Compare(proto, arg);
  if (!eq) {
    *result_listener << differences;
  }
  return eq;
}

class NullErrorCollector : public google::protobuf::DescriptorPool::ErrorCollector {
  void AddError(const std::string& filename, const std::string& element_name,
                const google::protobuf::Message* descriptor, ErrorLocation location,
                const std::string& message) override {}
  void RecordWarning(absl::string_view filename, absl::string_view element_name,
                     const google::protobuf::Message* descriptor, ErrorLocation location,
                     absl::string_view message) override {}
};

static void AddFile(google::protobuf::FileDescriptorProto& file, upb::DefPool* pool,
                    google::protobuf::DescriptorPool* desc_pool) {
  NullErrorCollector collector;
  const google::protobuf::FileDescriptor* file_desc =
      desc_pool->BuildFileCollectingErrors(file, &collector);

  if (file_desc != nullptr) {
    // The file descriptor was valid according to proto2.
    google::protobuf::FileDescriptorProto normalized_file;
    file_desc->CopyTo(&normalized_file);
    std::string serialized;
    normalized_file.SerializeToString(&serialized);
    upb::Arena arena;
    upb::Status status;
    google_protobuf_FileDescriptorProto* proto = google_protobuf_FileDescriptorProto_parse(
        serialized.data(), serialized.size(), arena.ptr());
    ASSERT_NE(proto, nullptr);
    upb::FileDefPtr file_def = pool->AddFile(proto, &status);

    // Ideally we could assert that file_def is present here.  After all, any
    // descriptor accepted by C++ should be by definition valid.  However C++
    // performs some of its validation at the .proto file parser level instead
    // of when validating descriptors.  As as result, C++ will accept some
    // unreasonable descriptors like:
    //   file { name: "" package: "0" }
    //
    // There is no .proto file that will produce this descriptor, but
    // BuildFile() accepts it.  We should probably clean up these cases so C++
    // will reject them too.
    if (!file_def) return;

    ASSERT_TRUE(status.ok()) << status.error_message();
    google_protobuf_FileDescriptorProto* upb_proto =
        upb_FileDef_ToProto(file_def.ptr(), arena.ptr());
    size_t size;
    const char* buf =
        google_protobuf_FileDescriptorProto_serialize(upb_proto, arena.ptr(), &size);
    google::protobuf::FileDescriptorProto google_proto;
    bool ok = google_proto.ParseFromArray(buf, size);
    ASSERT_TRUE(ok);
    EXPECT_THAT(google_proto, EqualsProtoTreatNansAsEqual(normalized_file));
  } else {
    // This file was invalid according to proto2.  When we parse it with upb,
    // it may or may not be accepted, since upb does not perform as much
    // validation as proto2.  However it must not crash.
    std::string serialized;
    file.SerializeToString(&serialized);
    upb::Arena arena;
    upb::Status status;
    google_protobuf_FileDescriptorProto* proto = google_protobuf_FileDescriptorProto_parse(
        serialized.data(), serialized.size(), arena.ptr());
    ASSERT_NE(proto, nullptr);
    pool->AddFile(proto, &status);
  }
}

inline void RoundTripDescriptor(const google::protobuf::FileDescriptorSet& set) {
  upb::DefPool defpool;
  google::protobuf::DescriptorPool desc_pool;
  desc_pool.EnforceWeakDependencies(true);
  for (const auto& file : set.file()) {
    google::protobuf::FileDescriptorProto mutable_file(file);
    AddFile(mutable_file, &defpool, &desc_pool);
  }
}

}  // namespace upb_test

#endif  // UPB_UTIL_DEF_TO_PROTO_TEST_H_
