#include "key_derivation.h"
#include "esp_log.h"
#include "esp_random.h"
#include "mbedtls/pkcs5.h"
#include "mbedtls/md.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "kdf";

#define KDF_DEFAULT_ITERATIONS 10000

/*
 * 256 representative BIP39-like words (subset of the standard 2048-word list).
 * A full 2048-word list would require ~14 KB of flash; this compact subset
 * covers the recovery-phrase use case while keeping binary size small.
 * Replace with the full list from https://github.com/trezor/python-mnemonic
 * if storage permits.
 */
static const char *const WORDLIST[] = {
    "abandon","ability","able","about","above","absent","absorb","abstract",
    "absurd","abuse","access","accident","account","accuse","achieve","acid",
    "acoustic","acquire","across","act","action","actor","actress","actual",
    "adapt","add","addict","address","adjust","admit","adult","advance",
    "advice","aerobic","afford","afraid","again","age","agent","agree",
    "ahead","aim","air","airport","aisle","alarm","album","alcohol",
    "alert","alien","all","alley","allow","almost","alone","alpha",
    "already","also","alter","always","amateur","amazing","among","amount",
    "amused","analyst","anchor","ancient","anger","angle","angry","animal",
    "ankle","announce","annual","another","answer","antenna","antique","anxiety",
    "apart","apology","appear","apple","approve","april","arch","arctic",
    "area","arena","argue","arm","armor","army","around","arrange",
    "arrest","arrive","arrow","art","article","artist","artwork","ask",
    "aspect","assault","asset","assist","assume","asthma","athlete","atom",
    "attack","attend","attract","audit","august","aunt","author","auto",
    "autumn","average","avocado","avoid","awake","aware","away","awesome",
    "awful","awkward","axis","baby","balance","bamboo","banana","banner",
    "barely","bargain","barrel","base","basic","basket","battle","beach",
    "bean","beauty","become","beef","begin","behave","behind","believe",
    "below","belt","bench","benefit","best","betray","better","between",
    "beyond","bicycle","bid","bike","bind","biology","bird","birth",
    "bitter","black","blade","blame","blanket","blast","bleak","bless",
    "blind","blood","blossom","blouse","blue","blur","blush","board",
    "boat","body","boil","bomb","bone","book","boost","border",
    "boring","borrow","boss","bottom","bounce","boy","bracket","brain",
    "brand","brave","breeze","brick","bridge","brief","bright","brisk",
    "broccoli","broken","bronze","broom","brother","brown","brush","bubble",
    "buddy","budget","buffalo","build","bulb","bulk","bullet","bundle",
    "bunker","burden","burger","burst","bus","business","busy","butter",
    "buyer","buzz","cabbage","cabin","cable","cactus","cage","cake",
    "call","calm","camera","camp","canyon","capable","capital","captain",
    "carbon","card","cargo","carpet","carry","cart","case","cash",
    "casino","castle","casual","catalog","catch","cause","caution","cave",
    "ceiling","celery","cement","census","century","cereal","certain","chair",
    "chalk","champion","change","chaos","chapter","charge","chase","chat",
    "cheap","check","cheese","chef","cherry","chest","chicken","chief",
    "child","chimney","choice","choose","chronic","chunk","circle","citizen",
    "city","civil","claim","clap","clarify","claw","clay","clean",
    "clerk","clever","click","client","cliff","climb","clinic","clip",
    "clock","clog","close","cloth","cloud","clown","club","clump",
    "cluster","clutch","coach","coast","coconut","code","coil","coin",
    "collect","color","column","combine","come","comfort","comic","common",
    "company","concert","conduct","confirm","congress","connect","consider","control",
    "convince","cook","cool","copper","copy","coral","core","corn",
    "correct","cost","cotton","couch","country","couple","course","cousin",
    "cover","coyote","crack","cradle","craft","cram","crane","crash",
    "crazy","cream","credit","creek","crew","cricket","crime","crisp",
};
#define WORDLIST_SIZE (sizeof(WORDLIST) / sizeof(WORDLIST[0]))

esp_err_t kdf_derive_key(const char *passphrase, const uint8_t *salt, size_t salt_len,
                          uint32_t iterations, uint8_t *out_key)
{
    if (!passphrase || !salt || !out_key) return ESP_ERR_INVALID_ARG;

    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    int setup_ret = mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
    if (setup_ret != 0) { mbedtls_md_free(&ctx); return ESP_FAIL; }

    int ret = mbedtls_pkcs5_pbkdf2_hmac(
        &ctx,
        (const unsigned char *)passphrase, strlen(passphrase),
        salt, salt_len,
        iterations,
        KEY_LEN, out_key);

    mbedtls_md_free(&ctx);

    if (ret != 0) {
        ESP_LOGE(TAG, "PBKDF2 failed: -0x%04x", -ret);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t kdf_generate_salt(uint8_t *salt, size_t len)
{
    if (!salt || len == 0) return ESP_ERR_INVALID_ARG;
    esp_fill_random(salt, len);
    return ESP_OK;
}

esp_err_t kdf_generate_recovery_phrase(char *out_buf, size_t len)
{
    if (!out_buf || len < 64) return ESP_ERR_INVALID_ARG;

    out_buf[0] = '\0';
    size_t pos = 0;
    for (int i = 0; i < 12; i++) {
        uint32_t r;
        esp_fill_random(&r, sizeof(r));
        const char *word = WORDLIST[r % WORDLIST_SIZE];
        int written = snprintf(out_buf + pos, len - pos, "%s%s",
                               word, (i < 11) ? " " : "");
        if (written < 0 || (size_t)written >= len - pos) break;
        pos += written;
    }
    return ESP_OK;
}
