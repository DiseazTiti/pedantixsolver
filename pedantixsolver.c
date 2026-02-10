#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* Symboles générés par ld -r -b binary à partir de wikipedia.bin */
extern const unsigned char _binary_wikipedia_bin_start[];
extern const unsigned char _binary_wikipedia_bin_end[];

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

/*
 * Compare un préfixe de longueurs de mots avec le pattern d'un nœud.
 *  -1  : prefix <  pattern  (chercher à gauche)
 *   0  : prefix est un préfixe de pattern  (match)
 *  +1  : prefix >  pattern  (chercher à droite)
 */
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

/*
 * Layout d'un nœud dans le .bin :
 *   [left_off : u32] [right_off : u32]
 *   [title_len : u16] [title : title_len octets]
 *   [pattern_len : u16] [pattern : pattern_len octets]
 */

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

int main(int argc, char *argv[]) {
    if (argc < 3 || strcmp(argv[1], "-p") != 0) {
        fprintf(stderr, "Usage : %s -p <len1> <len2> ...\n", argv[0]);
        fprintf(stderr, "Exemple : %s -p 1 3 4 6 7\n", argv[0]);
        return 1;
    }

    int plen = argc - 2;
    uint8_t *prefix = malloc(plen);
    for (int i = 0; i < plen; i++) {
        int v = atoi(argv[i + 2]);
        if (v < 0 || v > 255) {
            fprintf(stderr, "Erreur : les longueurs doivent être entre 0 et 255\n");
            free(prefix);
            return 1;
        }
        prefix[i] = (uint8_t)v;
    }

    g_data = _binary_wikipedia_bin_start;
    uint32_t node_count = read_u32(0);
    uint32_t root_off   = read_u32(4);

    (void)node_count; /* utilisé uniquement pour info éventuelle */

    Results res;
    results_init(&res);
    search(root_off, prefix, plen, &res);

    if (res.count == 0) {
        printf("Aucun résultat trouvé.\n");
    } else if (res.count == 1) {
        printf("Trouvé : %s\n", res.titles[0]);
    } else if (res.count <= MAX_DISPLAY) {
        printf("%d possibilités :\n", res.count);
        for (int i = 0; i < res.count; i++)
            printf("  %d. %s\n", i + 1, res.titles[i]);
    } else {
        printf("Plus de %d résultats, veuillez préciser davantage de longueurs de mots.\n",
               MAX_DISPLAY);
    }

    results_free(&res);
    free(prefix);
    return 0;
}
