// This file is licensed under the Elastic License 2.0. Copyright 2021-present, StarRocks Inc.

#include "exprs/vectorized/encryption_functions.h"

#include <glog/logging.h>
#include <gtest/gtest.h>

#include "butil/time.h"
#include "exprs/vectorized/mock_vectorized_expr.h"
#include "exprs/vectorized/string_functions.h"

namespace starrocks {
namespace vectorized {

class EncryptionFunctionsTest : public ::testing::Test {
public:
    void SetUp() {}
};

TEST_F(EncryptionFunctionsTest, aes_encryptGeneralTest) {
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    Columns columns;
    auto plain = BinaryColumn::create();
    auto text = BinaryColumn::create();

    std::string plains[] = {"key", "kewfewy", "apacheejian"};
    std::string texts[] = {"key", "doris342422131ey", "naixuex"};
    std::string results[] = {"CEF5BE724B7B98B63216C95A7BD681C9", "424B4E9B042FC5274A77A82BB4BB9826",
                             "09529C15ECF0FC27073310DCEB76FAF4"};

    for (int j = 0; j < sizeof(plains) / sizeof(plains[0]); ++j) {
        plain->append(plains[j]);
        text->append(texts[j]);
    }

    columns.emplace_back(plain);
    columns.emplace_back(text);

    ColumnPtr result = EncryptionFunctions::aes_encrypt(ctx.get(), columns);

    columns.clear();
    columns.emplace_back(result);
    result = StringFunctions::hex_string(ctx.get(), columns);

    auto v = ColumnHelper::cast_to<TYPE_VARCHAR>(result);

    for (int j = 0; j < sizeof(results) / sizeof(results[0]); ++j) {
        ASSERT_EQ(results[j], v->get_data()[j].to_string());
    }
}

TEST_F(EncryptionFunctionsTest, aes_encryptSingularCasesTest) {
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    Columns columns;
    auto plain = BinaryColumn::create();
    auto text = BinaryColumn::create();
    auto null_column = NullColumn::create();

    std::string plains[] = {"key", "kewfewy", "apacheejian", "", ""};
    std::string texts[] = {"key", "doris342422131ey", "naixuex", "", ""};
    std::string results[] = {"CEF5BE724B7B98B63216C95A7BD681C9", "424B4E9B042FC5274A77A82BB4BB9826",
                             "09529C15ECF0FC27073310DCEB76FAF4", "0143DB63EE66B0CDFF9F69917680151E",
                             "0143DB63EE66B0CDFF9F69917680151E"};

    for (int j = 0; j < sizeof(plains) / sizeof(plains[0]); ++j) {
        plain->append(plains[j]);
        if (j % 2 == 0) {
            null_column->append(DATUM_NOT_NULL);
            text->append(texts[j]);
        } else {
            null_column->append(DATUM_NULL);
            text->append_default();
        }
    }

    auto nullable_text = NullableColumn::create(text, null_column);
    columns.emplace_back(plain);
    columns.emplace_back(nullable_text);

    ColumnPtr result = EncryptionFunctions::aes_encrypt(ctx.get(), columns);

    columns.clear();
    columns.emplace_back(result);
    result = StringFunctions::hex_string(ctx.get(), columns);
    ASSERT_TRUE(result->is_nullable());
    for (int j = 0; j < sizeof(results) / sizeof(results[0]); ++j) {
        if (j % 2 == 0) {
            ASSERT_FALSE(result->is_null(j));
            auto datum = result->get(j);
            ASSERT_EQ(results[j], datum.get_slice().to_string());
        } else {
            ASSERT_TRUE(result->is_null(j));
        }
    }
}

TEST_F(EncryptionFunctionsTest, aes_encryptBigDataTest) {
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    Columns columns;
    auto plain = BinaryColumn::create();
    auto text = BinaryColumn::create();

    std::string plains[] = {"1111111111111111", "ywef23apachedsfwfeejian", "93024jdfojdfojfwjf23ro23rrdvvj"};
    std::string texts[] = {"1", "navweefwfwefixuex", "mkmkemff324342fdsfsf"};
    std::string results[] = {"915FAA83990E2E62C7C9054DA1CFEA9BED4F45AD3D6BEE46FFBC256CA34670C0",
                             "9B247414C29023C0E208DD1C4914EEB1AD7912069B5F47EF7B4E1CBDDDE7551C",
                             "CB49B2B910DA7C511C559B241183471C3718BF908D1946600ED4B7CE729E2684"};

    for (int j = 0; j < sizeof(plains) / sizeof(plains[0]); ++j) {
        plain->append(plains[j]);
        text->append(texts[j]);
    }

    columns.emplace_back(plain);
    columns.emplace_back(text);

    ColumnPtr result = EncryptionFunctions::aes_encrypt(ctx.get(), columns);

    columns.clear();
    columns.emplace_back(result);
    result = StringFunctions::hex_string(ctx.get(), columns);

    auto v = ColumnHelper::cast_to<TYPE_VARCHAR>(result);

    for (int j = 0; j < sizeof(results) / sizeof(results[0]); ++j) {
        ASSERT_EQ(results[j], v->get_data()[j].to_string());
    }
}

TEST_F(EncryptionFunctionsTest, aes_encryptNullPlainTest) {
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    Columns columns;
    auto plain = BinaryColumn::create();
    auto plain_null = NullColumn::create();
    auto text = BinaryColumn::create();

    std::string plains[] = {"key", "kewfewy", "apacheejian"};
    std::string texts[] = {"key", "doris342422131ey", "naixuex"};
    std::string results[] = {"CEF5BE724B7B98B63216C95A7BD681C9", "424B4E9B042FC5274A77A82BB4BB9826",
                             "09529C15ECF0FC27073310DCEB76FAF4"};

    for (int j = 0; j < sizeof(plains) / sizeof(plains[0]); ++j) {
        plain->append(plains[j]);
        plain_null->append(0);
        text->append(texts[j]);
    }
    plain->append_default();
    plain_null->append(1);

    text->append_default();

    columns.emplace_back(NullableColumn::create(plain, plain_null));
    columns.emplace_back(text);

    auto result = EncryptionFunctions::aes_encrypt(ctx.get(), columns);

    columns.clear();
    columns.emplace_back(result);
    result = StringFunctions::hex_string(ctx.get(), columns);

    auto result2 = ColumnHelper::as_column<NullableColumn>(result);
    auto v = ColumnHelper::cast_to<TYPE_VARCHAR>(result2->data_column());

    int j;
    for (j = 0; j < sizeof(results) / sizeof(results[0]); ++j) {
        ASSERT_EQ(results[j], v->get_data()[j].to_string());
    }
    ASSERT_TRUE(result2->is_null(j));
}

TEST_F(EncryptionFunctionsTest, aes_encryptNullTextTest) {
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    Columns columns;
    auto plain = BinaryColumn::create();
    auto text = BinaryColumn::create();
    auto text_null = NullColumn::create();

    std::string plains[] = {"key", "kewfewy", "apacheejian"};
    std::string texts[] = {"key", "doris342422131ey", "naixuex"};
    std::string results[] = {"CEF5BE724B7B98B63216C95A7BD681C9", "424B4E9B042FC5274A77A82BB4BB9826",
                             "09529C15ECF0FC27073310DCEB76FAF4"};

    for (int j = 0; j < sizeof(plains) / sizeof(plains[0]); ++j) {
        plain->append(plains[j]);
        text->append(texts[j]);
        text_null->append(0);
    }
    plain->append_default();
    text->append_default();
    text_null->append(1);

    columns.emplace_back(plain);
    columns.emplace_back(NullableColumn::create(text, text_null));

    auto result = EncryptionFunctions::aes_encrypt(ctx.get(), columns);

    columns.clear();
    columns.emplace_back(result);
    result = StringFunctions::hex_string(ctx.get(), columns);

    auto result2 = ColumnHelper::as_column<NullableColumn>(result);
    auto v = ColumnHelper::cast_to<TYPE_VARCHAR>(result2->data_column());

    int j;
    for (j = 0; j < sizeof(results) / sizeof(results[0]); ++j) {
        ASSERT_EQ(results[j], v->get_data()[j].to_string());
    }
    ASSERT_TRUE(result2->is_null(j));
}

TEST_F(EncryptionFunctionsTest, aes_encryptConstTextTest) {
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    Columns columns;
    auto plain = BinaryColumn::create();
    auto text = ColumnHelper::create_const_column<TYPE_VARCHAR>("key", 1);

    std::string plains[] = {"key", "kewfewy", "apacheejian"};
    std::string results[] = {"CEF5BE724B7B98B63216C95A7BD681C9", "944EE45DA6CA9428A74E92A7A80BFA87",
                             "3D1967BC5A9BF290F77FE42733A29F6F"};

    for (int j = 0; j < sizeof(plains) / sizeof(plains[0]); ++j) {
        plain->append(plains[j]);
    }

    columns.emplace_back(plain);
    columns.emplace_back(text);

    ColumnPtr result = EncryptionFunctions::aes_encrypt(ctx.get(), columns);

    columns.clear();
    columns.emplace_back(result);

    result = StringFunctions::hex_string(ctx.get(), columns);
    auto v = ColumnHelper::cast_to<TYPE_VARCHAR>(result);

    for (int j = 0; j < sizeof(results) / sizeof(results[0]); ++j) {
        ASSERT_EQ(results[j], v->get_data()[j].to_string());
    }
}

TEST_F(EncryptionFunctionsTest, aes_encryptConstAllTest) {
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    Columns columns;
    auto plain = ColumnHelper::create_const_column<TYPE_VARCHAR>("sdkfljljl", 1);
    auto text = ColumnHelper::create_const_column<TYPE_VARCHAR>("vsdvf342423", 1);

    std::string results[] = {"71AB242103F038D433D392A7DE0909AB"};

    columns.emplace_back(plain);
    columns.emplace_back(text);

    ColumnPtr result = EncryptionFunctions::aes_encrypt(ctx.get(), columns);

    columns.clear();
    columns.emplace_back(result);
    result = StringFunctions::hex_string(ctx.get(), columns);

    auto v = ColumnHelper::as_column<ConstColumn>(result);
    auto data_column = ColumnHelper::cast_to<TYPE_VARCHAR>(v->data_column());

    for (int j = 0; j < sizeof(results) / sizeof(results[0]); ++j) {
        ASSERT_EQ(results[j], data_column->get_data()[j].to_string());
    }
}

TEST_F(EncryptionFunctionsTest, aes_decryptGeneralTest) {
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    Columns columns;
    auto plain = BinaryColumn::create();
    auto text = BinaryColumn::create();

    std::string plains[] = {"key", "kewfewy", "apacheejian"};
    std::string texts[] = {"key", "doris342422131ey", "naixuex"};
    std::string results[] = {"CEF5BE724B7B98B63216C95A7BD681C9", "424B4E9B042FC5274A77A82BB4BB9826",
                             "09529C15ECF0FC27073310DCEB76FAF4"};

    for (int j = 0; j < sizeof(results) / sizeof(results[0]); ++j) {
        plain->append(results[j]);
        text->append(texts[j]);
    }

    columns.emplace_back(plain);

    ColumnPtr result = StringFunctions::unhex(ctx.get(), columns);

    columns.clear();
    columns.emplace_back(result);
    columns.emplace_back(text);
    result = EncryptionFunctions::aes_decrypt(ctx.get(), columns);

    auto v = ColumnHelper::cast_to<TYPE_VARCHAR>(result);

    for (int j = 0; j < sizeof(plains) / sizeof(plains[0]); ++j) {
        ASSERT_EQ(plains[j], v->get_data()[j].to_string());
    }
}

TEST_F(EncryptionFunctionsTest, aes_decryptBigDataTest) {
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    Columns columns;
    auto plain = BinaryColumn::create();
    auto text = BinaryColumn::create();

    std::string plains[] = {"1111111111111111", "ywef23apachedsfwfeejian", "93024jdfojdfojfwjf23ro23rrdvvj"};
    std::string texts[] = {"1", "navweefwfwefixuex", "mkmkemff324342fdsfsf"};
    std::string results[] = {"915FAA83990E2E62C7C9054DA1CFEA9BED4F45AD3D6BEE46FFBC256CA34670C0",
                             "9B247414C29023C0E208DD1C4914EEB1AD7912069B5F47EF7B4E1CBDDDE7551C",
                             "CB49B2B910DA7C511C559B241183471C3718BF908D1946600ED4B7CE729E2684"};

    for (int j = 0; j < sizeof(results) / sizeof(results[0]); ++j) {
        plain->append(results[j]);
        text->append(texts[j]);
    }

    columns.emplace_back(plain);

    ColumnPtr result = StringFunctions::unhex(ctx.get(), columns);

    columns.clear();
    columns.emplace_back(result);
    columns.emplace_back(text);
    result = EncryptionFunctions::aes_decrypt(ctx.get(), columns);

    auto v = ColumnHelper::cast_to<TYPE_VARCHAR>(result);

    for (int j = 0; j < sizeof(plains) / sizeof(plains[0]); ++j) {
        ASSERT_EQ(plains[j], v->get_data()[j].to_string());
    }
}

TEST_F(EncryptionFunctionsTest, aes_decryptNullPlainTest) {
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    Columns columns;
    auto plain = BinaryColumn::create();
    auto plain_null = NullColumn::create();
    auto text = BinaryColumn::create();

    std::string plains[] = {"key", "kewfewy", "apacheejian"};
    std::string texts[] = {"key", "doris342422131ey", "naixuex"};
    std::string results[] = {"CEF5BE724B7B98B63216C95A7BD681C9", "424B4E9B042FC5274A77A82BB4BB9826",
                             "09529C15ECF0FC27073310DCEB76FAF4"};

    for (int j = 0; j < sizeof(results) / sizeof(results[0]); ++j) {
        plain->append(results[j]);
        plain_null->append(0);
        text->append(texts[j]);
    }
    plain->append_default();
    plain_null->append(1);
    text->append_default();

    columns.emplace_back(NullableColumn::create(plain, plain_null));

    ColumnPtr result = StringFunctions::unhex(ctx.get(), columns);

    columns.clear();
    columns.emplace_back(result);
    columns.emplace_back(text);
    result = EncryptionFunctions::aes_decrypt(ctx.get(), columns);

    auto result2 = ColumnHelper::as_column<NullableColumn>(result);
    auto v = ColumnHelper::cast_to<TYPE_VARCHAR>(result2->data_column());

    int j;
    for (j = 0; j < sizeof(plains) / sizeof(plains[0]); ++j) {
        ASSERT_EQ(plains[j], v->get_data()[j].to_string());
    }
    ASSERT_TRUE(result2->is_null(j));
}

TEST_F(EncryptionFunctionsTest, aes_decryptNullTextTest) {
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    Columns columns;
    auto plain = BinaryColumn::create();
    auto text = BinaryColumn::create();
    auto text_null = NullColumn::create();

    std::string plains[] = {"key", "kewfewy", "apacheejian"};
    std::string texts[] = {"key", "doris342422131ey", "naixuex"};
    std::string results[] = {"CEF5BE724B7B98B63216C95A7BD681C9", "424B4E9B042FC5274A77A82BB4BB9826",
                             "09529C15ECF0FC27073310DCEB76FAF4"};

    for (int j = 0; j < sizeof(results) / sizeof(results[0]); ++j) {
        plain->append(results[j]);
        text->append(texts[j]);
        text_null->append(0);
    }
    plain->append_default();
    text->append_default();
    text_null->append(1);

    columns.emplace_back(NullableColumn::create(plain, text_null));

    ColumnPtr result = StringFunctions::unhex(ctx.get(), columns);

    columns.clear();
    columns.emplace_back(result);
    columns.emplace_back(text);
    result = EncryptionFunctions::aes_decrypt(ctx.get(), columns);

    auto result2 = ColumnHelper::as_column<NullableColumn>(result);
    auto v = ColumnHelper::cast_to<TYPE_VARCHAR>(result2->data_column());

    int j;
    for (j = 0; j < sizeof(plains) / sizeof(plains[0]); ++j) {
        ASSERT_EQ(plains[j], v->get_data()[j].to_string());
    }
    ASSERT_TRUE(result2->is_null(j));
}

TEST_F(EncryptionFunctionsTest, aes_decryptConstTextTest) {
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    Columns columns;
    auto plain = BinaryColumn::create();
    auto text = ColumnHelper::create_const_column<TYPE_VARCHAR>("key", 1);

    std::string plains[] = {"key", "kewfewy", "apacheejian"};
    std::string results[] = {"CEF5BE724B7B98B63216C95A7BD681C9", "944EE45DA6CA9428A74E92A7A80BFA87",
                             "3D1967BC5A9BF290F77FE42733A29F6F"};

    for (int j = 0; j < sizeof(results) / sizeof(results[0]); ++j) {
        plain->append(results[j]);
    }

    columns.emplace_back(plain);

    ColumnPtr result = StringFunctions::unhex(ctx.get(), columns);

    columns.clear();
    columns.emplace_back(result);
    columns.emplace_back(text);

    result = EncryptionFunctions::aes_decrypt(ctx.get(), columns);

    auto v = ColumnHelper::cast_to<TYPE_VARCHAR>(result);

    for (int j = 0; j < sizeof(plains) / sizeof(plains[0]); ++j) {
        ASSERT_EQ(plains[j], v->get_data()[j].to_string());
    }
}

TEST_F(EncryptionFunctionsTest, aes_decryptConstAllTest) {
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    Columns columns;
    auto plain = ColumnHelper::create_const_column<TYPE_VARCHAR>("71AB242103F038D433D392A7DE0909AB", 1);
    auto text = ColumnHelper::create_const_column<TYPE_VARCHAR>("vsdvf342423", 1);

    std::string results[] = {"sdkfljljl"};

    columns.emplace_back(plain);

    ColumnPtr result = StringFunctions::unhex(ctx.get(), columns);

    columns.clear();
    columns.emplace_back(result);
    columns.emplace_back(text);

    result = EncryptionFunctions::aes_decrypt(ctx.get(), columns);

    auto v = ColumnHelper::as_column<ConstColumn>(result);

    auto data_column = ColumnHelper::cast_to<TYPE_VARCHAR>(v->data_column());

    for (int j = 0; j < sizeof(results) / sizeof(results[0]); ++j) {
        ASSERT_EQ(results[j], data_column->get_data()[j].to_string());
    }
}

TEST_F(EncryptionFunctionsTest, from_base64GeneralTest) {
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    Columns columns;
    auto plain = BinaryColumn::create();

    std::string plains[] = {"MQ==", "ZG9yaXN3ZXE=", "MzQ5dWlvbmZrbHduZWZr"};
    std::string results[] = {"1", "dorisweq", "349uionfklwnefk"};

    for (int j = 0; j < sizeof(plains) / sizeof(plains[0]); ++j) {
        plain->append(plains[j]);
    }

    columns.emplace_back(plain);

    ColumnPtr result = EncryptionFunctions::from_base64(ctx.get(), columns);

    auto v = ColumnHelper::cast_to<TYPE_VARCHAR>(result);

    for (int j = 0; j < sizeof(results) / sizeof(results[0]); ++j) {
        ASSERT_EQ(results[j], v->get_data()[j].to_string());
    }
}

TEST_F(EncryptionFunctionsTest, from_base64NullTest) {
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    Columns columns;
    auto plain = BinaryColumn::create();
    auto plain_null = NullColumn::create();

    std::string plains[] = {"MQ==", "ZG9yaXN3ZXE=", "MzQ5dWlvbmZrbHduZWZr"};
    std::string results[] = {"1", "dorisweq", "349uionfklwnefk"};

    for (int j = 0; j < sizeof(plains) / sizeof(plains[0]); ++j) {
        plain->append(plains[j]);
        plain_null->append(0);
    }
    plain->append_default();
    plain_null->append(1);

    columns.emplace_back(NullableColumn::create(plain, plain_null));

    ColumnPtr result = EncryptionFunctions::from_base64(ctx.get(), columns);

    auto result2 = ColumnHelper::as_column<NullableColumn>(result);
    auto v = ColumnHelper::cast_to<TYPE_VARCHAR>(result2->data_column());

    int j;
    for (j = 0; j < sizeof(results) / sizeof(results[0]); ++j) {
        ASSERT_EQ(results[j], v->get_data()[j].to_string());
    }
    result2->is_null(j);
}

TEST_F(EncryptionFunctionsTest, from_base64ConstTest) {
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    Columns columns;
    auto plain = ColumnHelper::create_const_column<TYPE_VARCHAR>("MzQ5dWlvbmZrbHduZWZr", 1);

    std::string results[] = {"349uionfklwnefk"};

    columns.emplace_back(plain);

    ColumnPtr result = EncryptionFunctions::from_base64(ctx.get(), columns);

    auto v = ColumnHelper::as_column<ConstColumn>(result);
    auto data_column = ColumnHelper::cast_to<TYPE_VARCHAR>(v->data_column());

    for (int j = 0; j < sizeof(results) / sizeof(results[0]); ++j) {
        ASSERT_EQ(results[j], data_column->get_data()[j].to_string());
    }
}

TEST_F(EncryptionFunctionsTest, to_base64Test) {
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    Columns columns;
    auto plain = BinaryColumn::create();

    std::string plains[] = {"1", "dorisweq", "349uionfklwnefk"};
    std::string results[] = {"MQ==", "ZG9yaXN3ZXE=", "MzQ5dWlvbmZrbHduZWZr"};

    for (int j = 0; j < sizeof(plains) / sizeof(plains[0]); ++j) {
        plain->append(plains[j]);
    }

    columns.emplace_back(plain);

    ColumnPtr result = EncryptionFunctions::to_base64(ctx.get(), columns);

    auto v = ColumnHelper::cast_to<TYPE_VARCHAR>(result);

    for (int j = 0; j < sizeof(results) / sizeof(results[0]); ++j) {
        ASSERT_EQ(results[j], v->get_data()[j].to_string());
    }
}

TEST_F(EncryptionFunctionsTest, to_base64NullTest) {
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    Columns columns;
    auto plain = BinaryColumn::create();
    auto plain_null = NullColumn::create();

    std::string plains[] = {"1", "dorisweq", "349uionfklwnefk"};
    std::string results[] = {"MQ==", "ZG9yaXN3ZXE=", "MzQ5dWlvbmZrbHduZWZr"};

    for (int j = 0; j < sizeof(plains) / sizeof(plains[0]); ++j) {
        plain->append(plains[j]);
        plain_null->append(0);
    }
    plain->append_default();
    plain_null->append(1);

    columns.emplace_back(NullableColumn::create(plain, plain_null));

    ColumnPtr result = EncryptionFunctions::to_base64(ctx.get(), columns);

    auto result2 = ColumnHelper::as_column<NullableColumn>(result);
    auto v = ColumnHelper::cast_to<TYPE_VARCHAR>(result2->data_column());

    int j;
    for (j = 0; j < sizeof(results) / sizeof(results[0]); ++j) {
        ASSERT_EQ(results[j], v->get_data()[j].to_string());
    }
    ASSERT_TRUE(result2->is_null(j));
}

TEST_F(EncryptionFunctionsTest, to_base64ConstTest) {
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    Columns columns;
    auto plain = ColumnHelper::create_const_column<TYPE_VARCHAR>("349uionfklwnefk", 1);

    std::string results[] = {"MzQ5dWlvbmZrbHduZWZr"};

    columns.emplace_back(plain);

    ColumnPtr result = EncryptionFunctions::to_base64(ctx.get(), columns);

    auto result2 = ColumnHelper::as_column<ConstColumn>(result)->data_column();
    auto v = ColumnHelper::cast_to<TYPE_VARCHAR>(result2);

    for (int j = 0; j < sizeof(results) / sizeof(results[0]); ++j) {
        ASSERT_EQ(results[j], v->get_data()[j].to_string());
    }
}

TEST_F(EncryptionFunctionsTest, md5GeneralTest) {
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    Columns columns;
    auto plain = BinaryColumn::create();

    std::string plains[] = {"dorisqq", "errankong"};
    std::string results[] = {"465f8101946b24bc012ce07b4d17a5da", "4402f1c78924499be8a48506c00dc070"};

    for (int j = 0; j < sizeof(plains) / sizeof(plains[0]); ++j) {
        plain->append(plains[j]);
    }

    columns.emplace_back(plain);

    ColumnPtr result = EncryptionFunctions::md5(ctx.get(), columns);

    auto v = ColumnHelper::cast_to<TYPE_VARCHAR>(result);

    for (int j = 0; j < sizeof(results) / sizeof(results[0]); ++j) {
        ASSERT_EQ(results[j], v->get_data()[j].to_string());
    }
}

TEST_F(EncryptionFunctionsTest, md5NullTest) {
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    Columns columns;
    auto plain = BinaryColumn::create();
    auto plain_null = NullColumn::create();

    std::string plains[] = {"dorisqq", "errankong"};
    std::string results[] = {"465f8101946b24bc012ce07b4d17a5da", "4402f1c78924499be8a48506c00dc070"};

    for (int j = 0; j < sizeof(plains) / sizeof(plains[0]); ++j) {
        plain->append(plains[j]);
        plain_null->append(0);
    }
    plain->append_default();
    plain_null->append(1);

    columns.emplace_back(NullableColumn::create(plain, plain_null));

    ColumnPtr result = EncryptionFunctions::md5(ctx.get(), columns);

    auto result2 = ColumnHelper::as_column<NullableColumn>(result);
    auto v = ColumnHelper::cast_to<TYPE_VARCHAR>(result2->data_column());

    int j;
    for (j = 0; j < sizeof(results) / sizeof(results[0]); ++j) {
        ASSERT_EQ(results[j], v->get_data()[j].to_string());
    }
    ASSERT_TRUE(result2->is_null(j));
}

TEST_F(EncryptionFunctionsTest, md5ConstTest) {
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    Columns columns;
    auto plain = ColumnHelper::create_const_column<TYPE_VARCHAR>("errankong", 1);

    std::string results[] = {"4402f1c78924499be8a48506c00dc070"};

    columns.emplace_back(plain);

    ColumnPtr result = EncryptionFunctions::md5(ctx.get(), columns);

    auto result2 = ColumnHelper::as_column<ConstColumn>(result);
    auto v = ColumnHelper::cast_to<TYPE_VARCHAR>(result2->data_column());

    for (int j = 0; j < sizeof(results) / sizeof(results[0]); ++j) {
        ASSERT_EQ(results[j], ColumnHelper::get_const_value<TYPE_VARCHAR>(result2));
    }
}

TEST_F(EncryptionFunctionsTest, md5sumTest) {
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    Columns columns;

    std::string plains[] = {"dorisqq", "1", "324", "2111"};
    std::string results[] = {"ebe1e817a42e312d89ed197c8c67b5f7"};

    for (int j = 0; j < sizeof(plains) / sizeof(plains[0]); ++j) {
        auto plain = BinaryColumn::create();
        plain->append(plains[j]);
        columns.emplace_back(plain);
    }

    ColumnPtr result = EncryptionFunctions::md5sum(ctx.get(), columns);

    auto v = ColumnHelper::cast_to<TYPE_VARCHAR>(result);

    for (int j = 0; j < sizeof(results) / sizeof(results[0]); ++j) {
        ASSERT_EQ(results[j], v->get_data()[j].to_string());
    }
}

TEST_F(EncryptionFunctionsTest, md5sumNullTest) {
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    Columns columns;

    std::string plains[] = {"dorisqq", "1", "324", "2111"};
    std::string results[] = {"ebe1e817a42e312d89ed197c8c67b5f7"};

    for (int j = 0; j < sizeof(plains) / sizeof(plains[0]); ++j) {
        auto plain = BinaryColumn::create();
        plain->append(plains[j]);
        columns.emplace_back(plain);
    }

    for (int j = 0; j < sizeof(plains) / sizeof(plains[0]); ++j) {
        auto plain = BinaryColumn::create();
        plain->append(plains[j]);
        auto plain_null = NullColumn::create();
        plain_null->append(1);
        columns.emplace_back(NullableColumn::create(plain, plain_null));
    }

    ColumnPtr result = EncryptionFunctions::md5sum(ctx.get(), columns);

    auto v = ColumnHelper::cast_to<TYPE_VARCHAR>(result);

    for (int j = 0; j < sizeof(results) / sizeof(results[0]); ++j) {
        ASSERT_EQ(results[j], v->get_data()[j].to_string());
    }
}

TEST_F(EncryptionFunctionsTest, md5sum_numericTest) {
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    Columns columns;

    std::string plains[] = {"dorisqq", "1", "324", "2111"};
    std::string results[] = {"313541553194712735798834777371609380343"};

    for (int j = 0; j < sizeof(plains) / sizeof(plains[0]); ++j) {
        auto plain = BinaryColumn::create();
        plain->append(plains[j]);
        columns.emplace_back(plain);
    }

    ColumnPtr result = EncryptionFunctions::md5sum_numeric(ctx.get(), columns);

    auto v = ColumnHelper::cast_to<TYPE_VARCHAR>(result);

    for (int j = 0; j < sizeof(results) / sizeof(results[0]); ++j) {
        ASSERT_EQ(results[j], v->get_data()[j].to_string());
    }
}

TEST_F(EncryptionFunctionsTest, md5sum_numericNullTest) {
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    Columns columns;

    std::string plains[] = {"dorisqq", "1", "324", "2111"};
    std::string results[] = {"313541553194712735798834777371609380343"};

    for (int j = 0; j < sizeof(plains) / sizeof(plains[0]); ++j) {
        auto plain = BinaryColumn::create();
        plain->append(plains[j]);
        columns.emplace_back(plain);
    }

    for (int j = 0; j < sizeof(plains) / sizeof(plains[0]); ++j) {
        auto plain = BinaryColumn::create();
        plain->append(plains[j]);
        auto plain_null = NullColumn::create();
        plain_null->append(1);
        columns.emplace_back(NullableColumn::create(plain, plain_null));
    }

    ColumnPtr result = EncryptionFunctions::md5sum_numeric(ctx.get(), columns);

    auto v = ColumnHelper::cast_to<TYPE_VARCHAR>(result);

    for (int j = 0; j < sizeof(results) / sizeof(results[0]); ++j) {
        ASSERT_EQ(results[j], v->get_data()[j].to_string());
    }
}

class ShaTestFixture : public ::testing::TestWithParam<std::tuple<std::string, int, std::string>> {};

TEST_P(ShaTestFixture, test_sha2) {
    auto [str, len, expected] = GetParam();

    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    Columns columns;

    auto plain = BinaryColumn::create();
    plain->append(str);

    ColumnPtr hash_length =
            len == -1 ? ColumnHelper::create_const_null_column(1) : ColumnHelper::create_const_column<TYPE_INT>(len, 1);

    if (str == "NULL") {
        columns.emplace_back(ColumnHelper::create_const_null_column(1));
    } else {
        columns.emplace_back(plain);
    }
    columns.emplace_back(hash_length);

    ctx->impl()->set_constant_columns(columns);
    ASSERT_TRUE(EncryptionFunctions::sha2_prepare(ctx.get(), FunctionContext::FunctionStateScope::FRAGMENT_LOCAL).ok());

    if (len != -1) {
        ASSERT_NE(nullptr, ctx->get_function_state(FunctionContext::FRAGMENT_LOCAL));
    } else {
        ASSERT_EQ(nullptr, ctx->get_function_state(FunctionContext::FRAGMENT_LOCAL));
    }

    ColumnPtr result = EncryptionFunctions::sha2(ctx.get(), columns);
    if (expected == "NULL") {
        std::cerr << result->debug_string() << std::endl;
        EXPECT_TRUE(result->is_null(0));
    } else {
        auto v = ColumnHelper::cast_to<TYPE_VARCHAR>(result);
        EXPECT_EQ(expected, v->get_data()[0].to_string());
    }

    ASSERT_TRUE(EncryptionFunctions::sha2_close(ctx.get(),
                                                FunctionContext::FunctionContext::FunctionStateScope::FRAGMENT_LOCAL)
                        .ok());
}

INSTANTIATE_TEST_SUITE_P(
        ShaTest, ShaTestFixture,
        ::testing::Values(
                // Invalid cases
                // -1 means null
                std::make_tuple("starrocks", -1, "NULL"), std::make_tuple("starrocks", 225, "NULL"),
                std::make_tuple("NULL", 1, "NULL"),

                // Normal cases
                std::make_tuple("starrocks", 224, "0057da608f56e8cdd3c22208a93cdda3e142279a694dfc53007e80f3"),
                std::make_tuple("20211119", 224, "b080f0657e5b67fd52b2f010328d2fad10775f81aa71c05313d46a24"),
                std::make_tuple("starrocks", 256, "87da3b6aefc0bd626a32626685dad2dba7435095f26c5a9628a6b13ced5721b0"),
                std::make_tuple("20211119", 256, "1deab4a6f88c6cbab900c2ae0a1da4f0e7e981f8b0f0680d8ec6c25155ab4885"),
                std::make_tuple("starrocks", 384,
                                "eda8e790960d9ff4fdc6f481ec57bf443c147bf092086006e98a2ab0108afbaaf8e6f51d197f988dd798d2"
                                "524b12de2c"),
                std::make_tuple("20211119", 384,
                                "6195d65242957bdf844e6623acabf2b0879c9cb282a9490ed332f7fdc41aedbda7802af06d07f38d7ed694"
                                "49d3ff5bf8"),
                std::make_tuple("starrocks", 512,
                                "9df77afa38c688166eaa7511440dd3a0b1c32918e9ae60b8c74e4b0f530852cd1a0facc610b71ebfcbe345"
                                "f5fa40983fe68a686144d2c6981b8a3fab1b045cd0"),
                std::make_tuple("20211119", 512,
                                "eaf18d26b2976216790d95b2942d15b7db5f926c7d62d35f24c98b8eedbe96f2e6241e5e4fdc6b7d9e7893"
                                "d94d86cd8a6f3bb6b1804c22097b337ecc24f6015e")));

} // namespace vectorized
} // namespace starrocks
