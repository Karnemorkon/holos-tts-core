#include <cassert>
#include <iostream>
#include "../src/core/utf8.h"

// M0: self-contained перевірки UTF-8 утиліт (без 12.5МБ stress.trie у репо).
// Golden-тести стресу/PCM проти Python — M1.
int main() {
    using namespace holos::utf8;
    std::string w = "\xD0\xB2\xD0\xBE\xD0\xB4\xD0\xB0"; // "вода" (4 codepoint, 8 bytes)
    assert(len(w) == 4);
    assert(byte_offset(w, 0) == 0);
    assert(byte_offset(w, 1) == 2);
    assert(byte_offset(w, 4) == 8);

    // insert_at_cp: вставка ПЕРЕД codepoint-позицією (як Python s[:p]+ins+s[p:])
    // "вода" (4 cp). p=4 -> в кінець.
    std::string s = insert_at_cp(w, 4, encode_u32(0x301));
    assert(len(s) == 5);

    // p=1 -> перед "о": "в" + acute + "ода"
    std::string s2 = insert_at_cp(w, 1, encode_u32(0x301));
    assert(len(s2) == 5);
    assert(s2.substr(0, 2) == w.substr(0, 2)); // "в" (2 байти) + acute

    // p=2 -> перед "д": "во" + acute + "да"
    std::string s3 = insert_at_cp(w, 2, encode_u32(0x301));
    assert(len(s3) == 5);
    assert(s3.substr(0, 4) == w.substr(0, 4)); // "во" (4 байти) + acute

    std::cout << "utf8 ok\n";
    return 0;
}
