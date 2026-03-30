#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <curl/curl.h>

/* Symboles générés par xxd -i à partir de wikipedia.bin */
extern const unsigned char wikipedia_bin[];
extern const unsigned int wikipedia_bin_len;

static const unsigned char *g_data;

static inline uint32_t read_u32(uint32_t off) {
    uint32_t v;
    memcpy(&v, g_data + off, 4);
    return v;
}

static inline uint16_t read_u16(uint32_t off) {
    uint16_t v;
    memcpy(&v, g_data + off, 2);
    return v;
}

/* ---- Buffer dynamique pour curl ---- */

typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} Buffer;

static void buf_init(Buffer *b) {
    b->cap  = 4096;
    b->len  = 0;
    b->data = malloc(b->cap);
}

static void buf_free(Buffer *b) { free(b->data); }

static size_t curl_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t total = size * nmemb;
    Buffer *b = userdata;
    while (b->len + total >= b->cap) {
        b->cap *= 2;
        b->data = realloc(b->data, b->cap);
    }
    memcpy(b->data + b->len, ptr, total);
    b->len += total;
    b->data[b->len] = '\0';
    return total;
}

/* ---- Extraction des longueurs depuis le HTML de Pedantix ---- */

/*
 * Le HTML contient :  <div id="article"><p><span class="w">    </span> ...
 * Chaque <span class="w"> contient N espaces = mot de longueur N.
 * On extrait uniquement le premier <p> non-vide du <div id="article">.
 */
static int extract_lengths_from_html(const char *html, uint8_t **out, int *out_len) {
    /* Trouver <div id="article"> */
    const char *article = strstr(html, "id=\"article\"");
    if (!article) {
        fprintf(stderr, "Erreur : impossible de trouver id=\"article\" dans le HTML.\n");
        return -1;
    }

    /* Trouver le premier <p> qui contient class="w" (skip les <p> vides) */
    const char *p_start = article;
    const char *p_end = NULL;
    while ((p_start = strstr(p_start, "<p>")) != NULL) {
        p_end = strstr(p_start, "</p>");
        if (!p_end) break;
        /* Vérifier si ce <p> contient des mots */
        const char *has_word = strstr(p_start, "class=\"w\"");
        if (has_word && has_word < p_end) {
            break; /* Trouvé un <p> avec du contenu */
        }
        p_start = p_end + 4; /* Passer au prochain <p> */
    }

    if (!p_start || !p_end) {
        fprintf(stderr, "Erreur : pas de <p> avec du contenu trouvé dans l'article.\n");
        return -1;
    }

    int cap = 256;
    int len = 0;
    uint8_t *lengths = malloc(cap);

    const char *cursor = p_start;
    while (cursor < p_end) {
        /* Chercher <span class="w"> */
        const char *span = strstr(cursor, "class=\"w\">");
        if (!span || span >= p_end) break;

        const char *content = span + 10; /* après class="w"> */
        /* Trouver le </span> fermant */
        const char *span_end = strstr(content, "</span>");
        if (!span_end || span_end >= p_end) break;

        /* Compter les caractères (espaces insécables UTF-8 = 2 octets chacun)
         * Le site ajoute 2 caractères de padding par mot. */
        int wlen = (int)(span_end - content) / 2 - 2;
        if (wlen > 0 && wlen <= 255) {
            if (len >= cap) {
                cap *= 2;
                lengths = realloc(lengths, cap);
            }
            lengths[len++] = (uint8_t)wlen;
        }

        cursor = span_end + 7; /* après </span> */
    }

    if (len == 0) {
        fprintf(stderr, "Erreur : aucun mot trouvé dans le premier paragraphe.\n");
        free(lengths);
        return -1;
    }

    *out = lengths;
    *out_len = len;
    return 0;
}

/* ---- Fetch URL avec curl ---- */

static int fetch_url(const char *url, Buffer *buf) {
    CURL *curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "Erreur : impossible d'initialiser curl.\n");
        return -1;
    }
    buf_init(buf);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, buf);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "pedantixsolver/1.0");

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) {
        fprintf(stderr, "Erreur curl : %s\n", curl_easy_strerror(res));
        buf_free(buf);
        return -1;
    }
    return 0;
}

/* ---- Résultats ---- */

typedef struct {
    char **titles;
    int count;
    int cap;
} Results;

static void results_init(Results *r) {
    r->count = 0;
    r->cap   = 16;
    r->titles = malloc(r->cap * sizeof(char *));
}

static void results_add(Results *r, const char *title, uint16_t len) {
    if (r->count >= r->cap) {
        r->cap *= 2;
        r->titles = realloc(r->titles, r->cap * sizeof(char *));
    }
    char *t = malloc(len + 1);
    memcpy(t, title, len);
    t[len] = '\0';
    r->titles[r->count++] = t;
}

static void results_free(Results *r) {
    for (int i = 0; i < r->count; i++)
        free(r->titles[i]);
    free(r->titles);
}

/* ---- Comparaison préfixe ---- */

static int prefix_cmp(const uint8_t *prefix, int plen,
                      const uint8_t *pat, int patlen) {
    int n = plen < patlen ? plen : patlen;
    for (int i = 0; i < n; i++) {
        if (prefix[i] < pat[i]) return -1;
        if (prefix[i] > pat[i]) return  1;
    }
    return (plen <= patlen) ? 0 : 1;
}

/* ---- Recherche BST ---- */

#define MAX_DISPLAY 10

static void search(uint32_t off, const uint8_t *prefix, int plen, Results *res) {
    if (off == 0 || res->count > MAX_DISPLAY)
        return;

    uint32_t left_off  = read_u32(off);
    uint32_t right_off = read_u32(off + 4);
    uint16_t title_len = read_u16(off + 8);
    const char *title  = (const char *)(g_data + off + 10);
    uint16_t pat_len   = read_u16(off + 10 + title_len);
    const uint8_t *pat = g_data + off + 10 + title_len + 2;

    int cmp = prefix_cmp(prefix, plen, pat, pat_len);

    if (cmp <= 0)
        search(left_off, prefix, plen, res);

    if (cmp == 0 && res->count <= MAX_DISPLAY)
        results_add(res, title, title_len);

    if (cmp >= 0)
        search(right_off, prefix, plen, res);
}

/* ---- Affichage du pattern ---- */

static void print_pattern(const uint8_t *prefix, int plen) {
    printf("Pattern détecté :");
    for (int i = 0; i < plen; i++)
        printf(" %d", prefix[i]);
    printf("\n");
}

/* ---- Main ---- */

static void usage(const char *prog) {
    fprintf(stderr, "Usage :\n");
    fprintf(stderr, "  %s -m <len1> <len2> ...   Recherche manuelle par longueurs\n", prog);
    fprintf(stderr, "  %s -a                     Récupère automatiquement depuis pedantix\n", prog);
    fprintf(stderr, "Exemple : %s -m 1 3 4 6 7\n", prog);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    uint8_t *prefix = NULL;
    int plen = 0;
    int need_free_prefix = 0;

    if (strcmp(argv[1], "-a") == 0) {
        /* Mode automatique : fetch depuis pedantix */
        printf("Récupération de https://pedantix.certitudes.org/ ...\n");
        Buffer buf;
        if (fetch_url("https://pedantix.certitudes.org/", &buf) != 0)
            return 1;

        uint8_t *full_prefix = NULL;
        int full_plen = 0;
        if (extract_lengths_from_html(buf.data, &full_prefix, &full_plen) != 0) {
            buf_free(&buf);
            return 1;
        }
        buf_free(&buf);
        
        prefix = full_prefix;
        plen = full_plen;
        print_pattern(prefix, plen);
        need_free_prefix = 1;

    } else if (strcmp(argv[1], "-m") == 0 && argc >= 3) {
        /* Mode manuel */
        plen = argc - 2;
        prefix = malloc(plen);
        need_free_prefix = 1;
        for (int i = 0; i < plen; i++) {
            int v = atoi(argv[i + 2]);
            if (v < 0 || v > 255) {
                fprintf(stderr, "Erreur : les longueurs doivent être entre 0 et 255\n");
                free(prefix);
                return 1;
            }
            prefix[i] = (uint8_t)v;
        }
    } else {
        usage(argv[0]);
        return 1;
    }

    g_data = wikipedia_bin;
    uint32_t node_count = read_u32(0);
    uint32_t root_off   = read_u32(4);
    (void)node_count;

    Results res;
    int is_auto_mode = (strcmp(argv[1], "-a") == 0);
    
    if (is_auto_mode) {
        /* Mode automatique : augmenter progressivement le nombre de caractères */
        int full_plen = plen;
        int current_len = (full_plen < 10) ? full_plen : 10;
        
        while (current_len <= full_plen) {
            results_init(&res);
            search(root_off, prefix, current_len, &res);
                        
            /* Si un seul résultat trouvé, on s'arrête */
            if (res.count == 1) {
                break;
            }
            
            /* Si aucun résultat ou plus de 10 résultats, on s'arrête */
            if (res.count == 0 || current_len >= full_plen) {
                break;
            }
            
            /* Sinon, continuer avec plus de caractères */
            results_free(&res);
            current_len += 10;
        }
    } else {
        /* Mode manuel : recherche simple */
        results_init(&res);
        search(root_off, prefix, plen, &res);
    }

    if (res.count == 0) {
        printf("Aucun résultat trouvé.\n");
    } else if (res.count == 1) {
        printf("Réponse : %s\n", res.titles[0]);
    } else if (res.count <= MAX_DISPLAY) {
        printf("%d possibilités :\n", res.count);
        for (int i = 0; i < res.count; i++)
            printf("   - %s\n", res.titles[i]);
    } else {
        printf("Plus de %d résultats, veuillez préciser davantage de longueurs de mots.\n",
               MAX_DISPLAY);
    }

    results_free(&res);
    if (need_free_prefix) free(prefix);
    return 0;
}
