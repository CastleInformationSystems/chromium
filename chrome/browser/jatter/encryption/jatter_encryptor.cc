#include "chrome/browser/jatter/encryption/jatter_encryptor.h"

#include "base/base64.h"
#include "base/strings/string_split.h"
#include "crypto/random.h"
#include "crypto/sha2.h"
#include "third_party/boringssl/src/include/openssl/evp.h"

namespace {
constexpr size_t kBlockSize = 16;
constexpr size_t kKeySize = 32;
}  // namespace

std::string JatterEncryptor::ApplyPKCS7Padding(const std::string& unpadded) {
  size_t pad_len = kBlockSize - (unpadded.length() % kBlockSize);
  std::string padded = unpadded;
  padded.append(pad_len, static_cast<char>(pad_len));
  return padded;
}

std::optional<std::string> JatterEncryptor::StripPKCS7Padding(const std::string& padded) {
  if (padded.empty()) return std::nullopt;
  
  uint8_t pad_len = padded.back();
  if (pad_len == 0 || pad_len > kBlockSize || pad_len > padded.length()) {
    return std::nullopt;
  }

  for (size_t i = padded.length() - pad_len; i < padded.length(); ++i) {
    if (padded[i] != pad_len) return std::nullopt;
  }
  return padded.substr(0, padded.length() - pad_len);
}

std::string JatterEncryptor::Encrypt(const std::string& plaintext, const std::string& decoded_key) {
  std::vector<uint8_t> iv(kBlockSize);
  crypto::RandBytes(iv);
  return EncryptWithIVForTesting(plaintext, decoded_key, iv);
}

std::string JatterEncryptor::EncryptWithIVForTesting(const std::string& plaintext, 
                                                     const std::string& decoded_key, 
                                                     const std::vector<uint8_t>& iv) {
  if (decoded_key.length() != kKeySize || iv.size() != kBlockSize) return "";

  std::string padded = ApplyPKCS7Padding(plaintext);
  std::vector<uint8_t> ciphertext(padded.length());

  bssl::ScopedEVP_CIPHER_CTX ctx;
  EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_ctr(), nullptr,
                     reinterpret_cast<const uint8_t*>(decoded_key.data()), iv.data());

  int out_len1 = 0;
  EVP_EncryptUpdate(ctx.get(), ciphertext.data(), &out_len1,
                    reinterpret_cast<const uint8_t*>(padded.data()), padded.length());

  int out_len2 = 0;
  EVP_EncryptFinal_ex(ctx.get(), ciphertext.data() + out_len1, &out_len2);

  std::string iv_b64 = base::Base64Encode(iv);
  std::string ct_b64 = base::Base64Encode(ciphertext);

  return iv_b64 + ":" + ct_b64;
}

std::optional<std::string> JatterEncryptor::Decrypt(const std::string& formatted, 
                                                    const std::string& decoded_key) {
  if (decoded_key.length() != kKeySize) return std::nullopt;

  std::vector<std::string> parts = base::SplitString(
      formatted, ":", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
  
  if (parts.size() != 2) return std::nullopt;

  std::string iv;
  std::string ciphertext;
  if (!base::Base64Decode(parts[0], &iv) || iv.length() != kBlockSize ||
      !base::Base64Decode(parts[1], &ciphertext)) {
    return std::nullopt;
  }

  std::vector<uint8_t> padded(ciphertext.length());
  bssl::ScopedEVP_CIPHER_CTX ctx;
  EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_ctr(), nullptr,
                     reinterpret_cast<const uint8_t*>(decoded_key.data()),
                     reinterpret_cast<const uint8_t*>(iv.data()));

  int out_len1 = 0;
  EVP_DecryptUpdate(ctx.get(), padded.data(), &out_len1,
                    reinterpret_cast<const uint8_t*>(ciphertext.data()), ciphertext.length());

  int out_len2 = 0;
  EVP_DecryptFinal_ex(ctx.get(), padded.data() + out_len1, &out_len2);

  std::string padded_str(reinterpret_cast<char*>(padded.data()), ciphertext.length());
  return StripPKCS7Padding(padded_str);
}

std::string JatterEncryptor::DeriveSignature(const std::string& decoded_key) {
  std::string key_sha256 = crypto::SHA256HashString(decoded_key);
  std::string base64_sha256 = base::Base64Encode(key_sha256);
  
  std::vector<uint8_t> zero_iv(kBlockSize, 0);
  
  std::string formatted = EncryptWithIVForTesting(base64_sha256, decoded_key, zero_iv);
  
  std::vector<std::string> parts = base::SplitString(
      formatted, ":", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
      
  return parts.size() == 2 ? parts[1] : "";
}
