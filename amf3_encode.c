/*
 ** Copyright (C) 2010, 2013 Arseny Vakhrushev <arseny.vakhrushev at gmail dot com>
 ** Please read the LICENSE file for license details
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_amf3.h"
#include "zend_smart_str.h"
#include "ext/spl/php_spl.h"
#include "amf3.h"


typedef struct _amf_ref_class_def {
    uint32_t id;
    char encoding;
    HashTable *property_names;
} amf_ref_class_def;

typedef struct {
    HashTable strings, objects, traits, active_arrays;
    zend_long options;
    unsigned int depth;
    bool failed;
} EncodeContext;

static void encodeValue(smart_str *ss, zval *val, EncodeContext *ctx);

static void encodeU29(smart_str *ss, int val) {
    char buf[4];
    int size;
    val &= 0x1fffffff;
    if (val <= 0x7f) {
        buf[0] = val;
        size = 1;
    } else if (val <= 0x3fff) {
        buf[1] = val & 0x7f;
        val >>= 7;
        buf[0] = val | 0x80;
        size = 2;
    } else if (val <= 0x1fffff) {
        buf[2] = val & 0x7f;
        val >>= 7;
        buf[1] = val | 0x80;
        val >>= 7;
        buf[0] = val | 0x80;
        size = 3;
    } else {
        buf[3] = val;
        val >>= 8;
        buf[2] = val | 0x80;
        val >>= 7;
        buf[1] = val | 0x80;
        val >>= 7;
        buf[0] = val | 0x80;
        size = 4;
    }
    smart_str_appendl(ss, buf, size);
}

static void encodeDouble(smart_str *ss, double val) {
    union { int i; char c; } t;
    union { double d; char c[8]; } u;
    char buf[8];
    t.i = 1;
    u.d = val;
    if (!t.c) memcpy(buf, u.c, 8);
    else { /* little-endian machine */
        int i;
        for (i = 0; i < 8; ++i) buf[7 - i] = u.c[i];
    }
    smart_str_appendl(ss, buf, 8);
}

static int encodeRefStr(smart_str *ss, const char *str, size_t len, HashTable *ht) {
    int *oidx, nidx;
    if ((oidx = zend_hash_str_find_ptr(ht, str, len))) {
        encodeU29(ss, *oidx << 1);
        return 1;
    }
    nidx = zend_hash_num_elements(ht);
    if (nidx <= AMF3_MAX_INT) zend_hash_str_add_mem(ht, str, len, &nidx, sizeof(nidx));
    return 0;
}

static int encodeRefObj(smart_str *ss, zval *val, HashTable *ht) {
    int *oidx, nidx;
    zend_string *objectHash;

    objectHash = php_spl_object_hash(Z_OBJ_P(val));
    if ((oidx = zend_hash_str_find_ptr(ht, ZSTR_VAL(objectHash), ZSTR_LEN(objectHash)))) {
        zend_string_release(objectHash);
        encodeU29(ss, *oidx << 1);
        return 1;
    }

    nidx = zend_hash_num_elements(ht);
    if (nidx <= AMF3_MAX_INT) zend_hash_str_add_mem(ht, ZSTR_VAL(objectHash), ZSTR_LEN(objectHash), &nidx, sizeof(nidx));
    zend_string_release(objectHash);
    return 0;
}

static void encodeStr(smart_str *ss, const char *str, size_t len, HashTable *ht) {
    if (len > AMF3_MAX_INT) len = AMF3_MAX_INT;
    if (len && encodeRefStr(ss, str, len, ht)) return; /* Empty string is never sent by reference */
    encodeU29(ss, (len << 1) | 1);
    smart_str_appendl(ss, str, len);
}

/* Arrays occupy an AMF object-table slot even though PHP arrays are sent by value. */
static void reserveArrayRef(HashTable *ht) {
    int nidx = zend_hash_num_elements(ht);
    zend_hash_next_index_insert_mem(ht, &nidx, sizeof(nidx));
}

static bool encodeFailed(EncodeContext *ctx) {
    return ctx->failed || EG(exception);
}

static void encodeArray(smart_str *ss, zval *val, EncodeContext *ctx) {
    HashTable *ht = Z_ARRVAL_P(val);
    zval *hv;
    zend_string *key;
    int numeric_count = 0;

    reserveArrayRef(&ctx->objects);
    ZEND_HASH_FOREACH_STR_KEY(ht, key) {
        if (!key) ++numeric_count;
    } ZEND_HASH_FOREACH_END();
    if (numeric_count > AMF3_MAX_INT) {
        ctx->failed = true;
        php_error_docref(NULL, E_WARNING, "Too many array elements");
        return;
    }
    encodeU29(ss, (numeric_count << 1) | 1);

    ZEND_HASH_FOREACH_STR_KEY_VAL(ht, key, hv) {
        if (key && ZSTR_LEN(key)) {
            encodeStr(ss, ZSTR_VAL(key), ZSTR_LEN(key), &ctx->strings);
            encodeValue(ss, hv, ctx);
            if (encodeFailed(ctx)) return;
        }
    } ZEND_HASH_FOREACH_END();
    smart_str_appendc(ss, 0x01);

    ZEND_HASH_FOREACH_STR_KEY_VAL(ht, key, hv) {
        if (!key) {
            encodeValue(ss, hv, ctx);
            if (encodeFailed(ctx)) return;
        }
    } ZEND_HASH_FOREACH_END();
}

static void encodeObject(smart_str *ss, zval *val, EncodeContext *ctx) {
    bool is_object = Z_TYPE_P(val) == IS_OBJECT;
    zend_class_entry *ce = is_object ? Z_OBJCE_P(val) : zend_standard_class_def;
    HashTable *ht, *names = NULL;
    zval *hv;
    zend_string *key;
    zend_ulong index;
    smart_str trait_key = {0};
    amf_ref_class_def *def;

    if (is_object) {
        if (encodeRefObj(ss, val, &ctx->objects)) return;
        ht = Z_OBJ_HT_P(val)->get_properties(Z_OBJ_P(val));
        if (EG(exception)) return;
        if (!ht) {
            ctx->failed = true;
            php_error_docref(NULL, E_WARNING, "Invalid object properties");
            return;
        }
    } else {
        reserveArrayRef(&ctx->objects);
        ht = Z_ARRVAL_P(val);
    }

    /* A class can have different initialized/dynamic properties on each instance. */
    smart_str_appendl(&trait_key, (const char *)&ce, sizeof(ce));
    if (ce != zend_standard_class_def) {
        names = zend_new_array(zend_hash_num_elements(ht));
        ZEND_HASH_FOREACH_STR_KEY_VAL(ht, key, hv) {
            bool is_dynamic = Z_TYPE_P(hv) != IS_INDIRECT;
            ZVAL_DEINDIRECT(hv);
            if (Z_ISUNDEF_P(hv)) continue;
            if (!key) {
                php_error_docref(NULL, E_WARNING, "num property");
                if (EG(exception)) break;
                continue;
            }
            if (!ZSTR_LEN(key) || !ZSTR_VAL(key)[0] || ZSTR_VAL(key)[0] == '_') continue;
            if (zend_check_property_access(Z_OBJ_P(val), key, is_dynamic) == FAILURE) continue;
            zend_hash_add_empty_element(names, key);
            size_t len = ZSTR_LEN(key);
            smart_str_appendl(&trait_key, (const char *)&len, sizeof(len));
            smart_str_append(&trait_key, key);
        } ZEND_HASH_FOREACH_END();
    }
    if (EG(exception)) {
        if (names) zend_array_destroy(names);
        smart_str_free(&trait_key);
        return;
    }

    def = zend_hash_find_ptr(&ctx->traits, trait_key.s);
    if (def) {
        if (names) zend_array_destroy(names);
        encodeU29(ss, (def->id << 2) | 1);
    } else {
        def = emalloc(sizeof(*def));
        def->id = zend_hash_num_elements(&ctx->traits);
        def->encoding = ce == zend_standard_class_def ? ET_DYNAMIC : ET_PROPLIST;
        def->property_names = names;
        zend_hash_add_ptr(&ctx->traits, trait_key.s, def);
        uint32_t count = names ? zend_hash_num_elements(names) : 0;
        encodeU29(ss, AMF3_OBJECT_ENCODING | (def->encoding << 2) | (count << 4));
        if (names) {
            encodeStr(ss, ZSTR_VAL(ce->name), ZSTR_LEN(ce->name), &ctx->strings);
            ZEND_HASH_FOREACH_STR_KEY(names, key) {
                encodeStr(ss, ZSTR_VAL(key), ZSTR_LEN(key), &ctx->strings);
            } ZEND_HASH_FOREACH_END();
        } else {
            smart_str_appendc(ss, 0x01);
        }
    }
    smart_str_free(&trait_key);

    if (def->encoding == ET_PROPLIST) {
        ZEND_HASH_FOREACH_STR_KEY(def->property_names, key) {
            hv = zend_hash_find(ht, key);
            if (!hv) {
                ctx->failed = true;
                php_error_docref(NULL, E_WARNING, "Object properties changed during encoding");
                return;
            }
            encodeValue(ss, hv, ctx);
            if (encodeFailed(ctx)) return;
        } ZEND_HASH_FOREACH_END();
    } else {
        ZEND_HASH_FOREACH_KEY_VAL(ht, index, key, hv) {
            if (key) {
                if (!ZSTR_LEN(key) || !ZSTR_VAL(key)[0] || ZSTR_VAL(key)[0] == '_') continue;
                encodeStr(ss, ZSTR_VAL(key), ZSTR_LEN(key), &ctx->strings);
            } else {
                char buf[32];
                size_t len = snprintf(buf, sizeof(buf), ZEND_LONG_FMT, (zend_long)index);
                encodeStr(ss, buf, len, &ctx->strings);
            }
            encodeValue(ss, hv, ctx);
            if (encodeFailed(ctx)) return;
        } ZEND_HASH_FOREACH_END();
        smart_str_appendc(ss, 0x01);
    }
}

static void encodeValue(smart_str *ss, zval *val, EncodeContext *ctx) {
    if (encodeFailed(ctx)) return;
    while (Z_TYPE_P(val) == IS_REFERENCE || Z_TYPE_P(val) == IS_INDIRECT) {
        val = Z_TYPE_P(val) == IS_REFERENCE ? Z_REFVAL_P(val) : Z_INDIRECT_P(val);
    }
    bool container = Z_TYPE_P(val) == IS_ARRAY || Z_TYPE_P(val) == IS_OBJECT;
    zend_ulong array_id = 0;
    if (container) {
        if (ctx->depth >= AMF3_MAX_DEPTH) {
            ctx->failed = true;
            php_error_docref(NULL, E_WARNING, "Maximum nesting depth of %d exceeded", AMF3_MAX_DEPTH);
            return;
        }
        if (Z_TYPE_P(val) == IS_ARRAY) {
            array_id = (zend_ulong)(uintptr_t)Z_ARRVAL_P(val);
            if (zend_hash_index_exists(&ctx->active_arrays, array_id)) {
                ctx->failed = true;
                php_error_docref(NULL, E_WARNING, "Recursive arrays cannot be encoded");
                return;
            }
            zend_hash_index_add_empty_element(&ctx->active_arrays, array_id);
        }
        ++ctx->depth;
    }
    switch (Z_TYPE_P(val)) {
        default:
            smart_str_appendc(ss, AMF3_UNDEFINED);
            break;
        case IS_NULL:
            smart_str_appendc(ss, AMF3_NULL);
            break;
        case IS_FALSE:
            smart_str_appendc(ss, AMF3_FALSE);
            break;
        case IS_TRUE:
            smart_str_appendc(ss, AMF3_TRUE);
            break;
        case IS_LONG:
            if ((Z_LVAL_P(val) >= AMF3_MIN_INT) && (Z_LVAL_P(val) <= AMF3_MAX_INT)) {
                smart_str_appendc(ss, AMF3_INTEGER);
                encodeU29(ss, Z_LVAL_P(val));
            } else {
                smart_str_appendc(ss, AMF3_DOUBLE);
                encodeDouble(ss, Z_LVAL_P(val));
            }
            break;
        case IS_DOUBLE:
            smart_str_appendc(ss, AMF3_DOUBLE);
            encodeDouble(ss, Z_DVAL_P(val));
            break;
        case IS_STRING:
            smart_str_appendc(ss, AMF3_STRING);
            encodeStr(ss, Z_STRVAL_P(val), Z_STRLEN_P(val), &ctx->strings);
            break;
        case IS_ARRAY:
            if (!(ctx->options & AMF3_FORCE_OBJECT)) {
                smart_str_appendc(ss, AMF3_ARRAY);
                encodeArray(ss, val, ctx);
                break;
            }
            ZEND_FALLTHROUGH;
        case IS_OBJECT:
            smart_str_appendc(ss, AMF3_OBJECT);
            encodeObject(ss, val, ctx);
            break;
    }
    if (container) {
        --ctx->depth;
        if (Z_TYPE_P(val) == IS_ARRAY) zend_hash_index_del(&ctx->active_arrays, array_id);
    }
}

static void freePtr(zval *val) {
    efree(Z_PTR_P(val));
}

static void freeTraitPtr(zval *val) {
    amf_ref_class_def *def = Z_PTR_P(val);

    if (def->property_names != NULL) {
        zend_array_destroy(def->property_names);
    }
    efree(def);
}

PHP_FUNCTION(amf3_encode) {
    smart_str ss = {0};
    zval *val;
    EncodeContext ctx = {0};
    if (zend_parse_parameters(ZEND_NUM_ARGS(), "z|l", &val, &ctx.options) == FAILURE) return;
    zend_hash_init(&ctx.strings, 0, NULL, freePtr, 0);
    zend_hash_init(&ctx.objects, 0, NULL, freePtr, 0);
    zend_hash_init(&ctx.traits, 0, NULL, freeTraitPtr, 0);
    zend_hash_init(&ctx.active_arrays, 0, NULL, NULL, 0);
    encodeValue(&ss, val, &ctx);
    zend_hash_destroy(&ctx.strings);
    zend_hash_destroy(&ctx.objects);
    zend_hash_destroy(&ctx.traits);
    zend_hash_destroy(&ctx.active_arrays);
    if (encodeFailed(&ctx)) {
        smart_str_free(&ss);
        RETURN_FALSE;
    }
    smart_str_0(&ss);
    RETURN_STR(ss.s);
}
