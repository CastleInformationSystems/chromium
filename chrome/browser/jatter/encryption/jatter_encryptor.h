#ifndef CHROME_BROWSER_JATTER_JATTER_ENCRYPTOR_H_
#define CHROME_BROWSER_JATTER_JATTER_ENCRYPTOR_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

class JatterEncryptor {
 public:
  // Encrypts plaintext using AES-256-CTR, a random 16-byte IV, and PKCS7 padding.
  // Returns formatted string: "base64(iv):base64(ciphertext)"
  static std::string Encrypt(const std::string& plaintext, const std::string& decoded_key);

  // Overload allowing a specific IV to be passed (used STRICTLY for unit testing vectors).
  static std::string EncryptWithIVForTesting(const std::string& plaintext, 
                                             const std::string& decoded_key, 
                                             const std::vector<uint8_t>& iv);

  // Decrypts the formatted string, strips PKCS7 padding, and returns plaintext.
  static std::optional<std::string> Decrypt(const std::string& formatted_ciphertext, 
                                            const std::string& decoded_key);

  // Derives the signature matching the user's account (Appendix A.2)
  static std::string DeriveSignature(const std::string& decoded_key);

 private:
  static std::string ApplyPKCS7Padding(const std::string& unpadded);
  static std::optional<std::string> StripPKCS7Padding(const std::string& padded);
};

#endif  // CHROME_BROWSER_JATTER_JATTER_ENCRYPTOR_H_
