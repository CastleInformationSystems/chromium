#include "chrome/browser/jatter/encryption/jatter_encryptor.h"
#include "base/base64.h"
#include "testing/gtest/include/gtest/gtest.h"

class JatterEncryptorTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(base::Base64Decode("AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=", &test_key_));
    
    standard_iv_.resize(16);
    for (int i = 0; i < 16; ++i) {
      standard_iv_[i] = 0x10 + i; // 0x10 to 0x1f
    }
  }

  std::string test_key_;
  std::vector<uint8_t> standard_iv_;
};

TEST_F(JatterEncryptorTest, Vector1) {
  std::string result = JatterEncryptor::EncryptWithIVForTesting("Hello, Jatter", test_key_, standard_iv_);
  EXPECT_EQ(result, "EBESExQVFhcYGRobHB0eHw==:oaaD5t0Yc6yRAOizROSrjQ==");
  EXPECT_EQ(JatterEncryptor::Decrypt(result, test_key_), "Hello, Jatter");
}

TEST_F(JatterEncryptorTest, Vector2_CounterCarry) {
  std::vector<uint8_t> carry_iv(16, 0xff);
  carry_iv[0] = 0x00;
  
  std::string result = JatterEncryptor::EncryptWithIVForTesting("Counter carry test: 0123456789abcdefghij", test_key_, carry_iv);
  EXPECT_EQ(result, "AP///////////////////w==:10KHkTHJrHJwhF04szfsSrTBI6RaIHMv4pkx/DvBYMqOUzImz4MfKl7ajzlZc2I9");
  EXPECT_EQ(JatterEncryptor::Decrypt(result, test_key_), "Counter carry test: 0123456789abcdefghij");
}

TEST_F(JatterEncryptorTest, Vector3_UTF8) {
  std::string result = JatterEncryptor::EncryptWithIVForTesting("Café ☕ 東京", test_key_, standard_iv_);
  EXPECT_EQ(result, "EBESExQVFhcYGRobHB0eHw==:qqKJSRsUsX5lVHpLhwMSIunRW4rrhNTxpuKAU007IMM=");
  EXPECT_EQ(JatterEncryptor::Decrypt(result, test_key_), "Café ☕ 東京");
}

TEST_F(JatterEncryptorTest, Vector4_VisitBlob) {
  std::string blob = R"({"url":"https://example.com/a?q=1","external_referrer_url":""})";
  std::string result = JatterEncryptor::EncryptWithIVForTesting(blob, test_key_, standard_iv_);
  EXPECT_EQ(result, "EBESExQVFhcYGRobHB0eHw==:kuGa+N4WacSYAOimRd2HoZy5KveL+KHP1Z39bDwUQe4XOlAwUrybGpdyF55zT7ZjAsvFW6GL7yvFkI8U33RBWA==");
  EXPECT_EQ(JatterEncryptor::Decrypt(result, test_key_), blob);
}

TEST_F(JatterEncryptorTest, SignatureDerivation) {
  std::string signature = JatterEncryptor::DeriveSignature(test_key_);
  EXPECT_EQ(signature, "q+cz+GEe/ZXkwfs4mEIlyYVqPMwels621rzjSwSgenR/hPLq4m715U3mmQgcKJWd");
}

TEST_F(JatterEncryptorTest, RoundTripRandomIV) {
  std::string plaintext = "Random IV round trip test";
  std::string encrypted = JatterEncryptor::Encrypt(plaintext, test_key_);
  EXPECT_EQ(JatterEncryptor::Decrypt(encrypted, test_key_), plaintext);
}
