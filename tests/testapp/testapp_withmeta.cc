/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2017 Couchbase, Inc.
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 */

#include <protocol/connection/client_mcbp_commands.h>
#include "testapp.h"
#include "testapp_client_test.h"

#include <utilities/string_utilities.h>
#include <xattr/blob.h>
#include <xattr/utils.h>

class WithMetaTest : public TestappXattrClientTest {
public:
    void SetUp() override {
        TestappXattrClientTest::SetUp();
        document.info.cas = testCas; // Must have a cas for meta ops
    }

    /**
     * Check the CAS of the set document against our value
     * using vattr for the lookup
     */
    void checkCas() {
        auto& conn = getConnection();
        BinprotSubdocCommand cmd;
        cmd.setOp(cb::mcbp::ClientOpcode::SubdocGet);
        cmd.setKey(name);
        cmd.setPath("$document");
        cmd.addPathFlags(SUBDOC_FLAG_XATTR_PATH);
        cmd.addDocFlags(mcbp::subdoc::doc_flag::None);

        auto resp = conn.execute(cmd);

        ASSERT_EQ(cb::mcbp::Status::Success, resp.getStatus());
        auto json = nlohmann::json::parse(resp.getDataString());
        EXPECT_STREQ(testCasStr, json["CAS"].get<std::string>().c_str());
    }

    /**
     * Make ::document an xattr value
     */
    void makeDocumentXattrValue() {
        cb::xattr::Blob blob;
        blob.set("user", "{\"author\":\"bubba\"}");
        blob.set("meta", "{\"content-type\":\"text\"}");

        auto xattrValue = blob.finalize();

        // append body to the xattrs and store in data
        std::string body = "document_body";
        document.value.clear();
        std::copy_n(xattrValue.buf,
                    xattrValue.len,
                    std::back_inserter(document.value));
        std::copy_n(
                body.c_str(), body.size(), std::back_inserter(document.value));
        cb::const_char_buffer xattr{(char*)document.value.data(),
                                    document.value.size()};

        document.info.datatype = cb::mcbp::Datatype::Xattr;

        if (hasSnappySupport() == ClientSnappySupport::Yes) {
            document.compress();
        }
    }

protected:
    const uint64_t testCas = 0xb33ff00dcafef00dull;
    const char* testCasStr = "0xb33ff00dcafef00d";
};

INSTANTIATE_TEST_CASE_P(
        TransportProtocols,
        WithMetaTest,
        ::testing::Combine(::testing::Values(TransportProtocols::McbpPlain,
                                             TransportProtocols::McbpSsl),
                           ::testing::Values(XattrSupport::Yes,
                                             XattrSupport::No),
                           ::testing::Values(ClientJSONSupport::Yes,
                                             ClientJSONSupport::No),
                           ::testing::Values(ClientSnappySupport::Yes,
                                             ClientSnappySupport::No)),
        PrintToStringCombinedName());

TEST_P(WithMetaTest, basicSet) {
    TESTAPP_SKIP_IF_UNSUPPORTED(cb::mcbp::ClientOpcode::SetWithMeta);

    MutationInfo resp;
    try {
        resp = getConnection().mutateWithMeta(document,
                                              Vbid(0),
                                              mcbp::cas::Wildcard,
                                              /*seqno*/ 1,
                                              /*options*/ 0,
                                              {});
    } catch (std::exception&) {
        FAIL() << "mutateWithMeta threw an exception";
    }

    if (::testing::get<1>(GetParam()) == XattrSupport::Yes) {
        checkCas();
    }
}

TEST_P(WithMetaTest, basicSetXattr) {
    TESTAPP_SKIP_IF_UNSUPPORTED(cb::mcbp::ClientOpcode::SetWithMeta);
    makeDocumentXattrValue();

    MutationInfo resp;
    try {
        resp = getConnection().mutateWithMeta(document,
                                              Vbid(0),
                                              mcbp::cas::Wildcard,
                                              /*seqno*/ 1,
                                              /*options*/ 0,
                                              {});
        EXPECT_EQ(XattrSupport::Yes, ::testing::get<1>(GetParam()));
        EXPECT_EQ(testCas, ntohll(resp.cas));
    } catch (std::exception&) {
        EXPECT_EQ(XattrSupport::No, ::testing::get<1>(GetParam()));
    }

    if (::testing::get<1>(GetParam()) == XattrSupport::Yes) {
        checkCas();
    }
}

TEST_P(WithMetaTest, MB36304_DocumetTooBig) {
    TESTAPP_SKIP_IF_UNSUPPORTED(cb::mcbp::ClientOpcode::SetWithMeta);
    if (::testing::get<3>(GetParam()) == ClientSnappySupport::No) {
        return;
    }

    std::vector<char> blob(21 * 1024 * 1024);
    cb::compression::Buffer deflated;
    ASSERT_TRUE(cb::compression::deflate(cb::compression::Algorithm::Snappy,
                                         {blob.data(), blob.size()},
                                         deflated));
    cb::const_char_buffer doc = deflated;
    document.value.clear();
    std::copy(doc.begin(), doc.end(), std::back_inserter(document.value));
    document.info.datatype = cb::mcbp::Datatype::Snappy;
    try {
        getConnection().mutateWithMeta(
                document, Vbid(0), mcbp::cas::Wildcard, 1, 0, {});
        FAIL() << "It should not be possible to store documents which exceeds "
                  "the max document size";
    } catch (const ConnectionError& error) {
        EXPECT_EQ(cb::mcbp::Status::E2big, error.getReason());
    }
}

TEST_P(WithMetaTest, MB36304_DocumentMaxSizeWithXattr) {
    TESTAPP_SKIP_IF_UNSUPPORTED(cb::mcbp::ClientOpcode::SetWithMeta);
    if (::testing::get<1>(GetParam()) == XattrSupport::No) {
        return;
    }

    document.value.clear();
    cb::xattr::Blob blob;
    blob.set("_sys", R"({"author":"bubba"})");
    auto xattrValue = blob.finalize();
    std::copy(xattrValue.begin(),
              xattrValue.end(),
              std::back_inserter(document.value));

    document.value.resize((20 * 1024 * 1024) + xattrValue.size());
    document.info.datatype = cb::mcbp::Datatype::Xattr;
    auto& conn = getConnection();
    conn.mutateWithMeta(document, Vbid(0), mcbp::cas::Wildcard, 1, 0, {});
    conn.remove(name, Vbid(0));
}
