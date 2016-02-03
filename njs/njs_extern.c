
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <nxt_types.h>
#include <nxt_clang.h>
#include <nxt_alignment.h>
#include <nxt_stub.h>
#include <nxt_utf8.h>
#include <nxt_djb_hash.h>
#include <nxt_array.h>
#include <nxt_lvlhsh.h>
#include <nxt_mem_cache_pool.h>
#include <njscript.h>
#include <njs_vm.h>
#include <njs_extern.h>
#include <njs_variable.h>
#include <njs_parser.h>
#include <string.h>


static nxt_int_t
njs_extern_hash_test(nxt_lvlhsh_query_t *lhq, void *data)
{
    njs_extern_t  *ext;

    ext = data;

// STUB
//    if (nxt_strcasestr_eq(&lhq->key, &ext->name)) {
    if (nxt_strstr_eq(&lhq->key, &ext->name)) {
        return NXT_OK;
    }

    return NXT_DECLINED;
}


const nxt_lvlhsh_proto_t  njs_extern_hash_proto
    nxt_aligned(64) =
{
    NXT_LVLHSH_DEFAULT,
    NXT_LVLHSH_BATCH_ALLOC,
    njs_extern_hash_test,
    njs_lvlhsh_alloc,
    njs_lvlhsh_free,
};


nxt_int_t
njs_add_external(nxt_lvlhsh_t *hash, nxt_mem_cache_pool_t *mcp,
    uintptr_t object, njs_external_t *external, nxt_uint_t n)
{
    nxt_int_t           ret;
    njs_extern_t        *ext;
    nxt_lvlhsh_query_t  lhq;

    do {
        ext = nxt_mem_cache_align(mcp, sizeof(njs_value_t),
                                 sizeof(njs_extern_t));
        if (nxt_slow_path(ext == NULL)) {
            return NXT_ERROR;
        }

        ext->name.len = external->name.len;
        ext->name.data = nxt_mem_cache_alloc(mcp, external->name.len);
        if (nxt_slow_path(ext->name.data == NULL)) {
            return NXT_ERROR;
        }

        memcpy(ext->name.data, external->name.data, external->name.len);

        ext->value.type = NJS_EXTERNAL;
        ext->value.data.truth = 1;
        ext->value.data.u.external = ext;

        if (external->method != NULL) {
            ext->function = nxt_mem_cache_zalloc(mcp, sizeof(njs_function_t));
            if (nxt_slow_path(ext->function == NULL)) {
                return NXT_ERROR;
            }

            ext->function->native = 1;
            ext->function->args_offset = 1;
            ext->function->u.native = external->method;
        }

        nxt_lvlhsh_init(&ext->hash);
        ext->type = external->type;
        ext->get = external->get;
        ext->set = external->set;
        ext->find = external->find;
        ext->foreach = external->foreach;
        ext->next = external->next;
        ext->object = object;
        ext->data = external->data;

        lhq.key_hash = nxt_djb_hash(external->name.data, external->name.len);
        lhq.key = ext->name;
        lhq.replace = 0;
        lhq.value = ext;
        lhq.pool = mcp;
        lhq.proto = &njs_extern_hash_proto;

        ret = nxt_lvlhsh_insert(hash, &lhq);
        if (nxt_slow_path(ret != NXT_OK)) {
            return ret;
        }

        if (external->properties != NULL) {
            ret = njs_add_external(&ext->hash, mcp, object,
                                   external->properties, external->nproperties);
            if (nxt_slow_path(ret != NXT_OK)) {
                return ret;
            }
        }

        external++;
        n--;

    } while (n != 0);

    return NXT_OK;
}


njs_extern_t *
njs_parser_external(njs_vm_t *vm, njs_parser_t *parser)
{
    nxt_lvlhsh_query_t  lhq;

    lhq.key_hash = parser->lexer->key_hash;
    lhq.key = parser->lexer->text;
    lhq.proto = &njs_extern_hash_proto;

    if (nxt_lvlhsh_find(&vm->externals_hash, &lhq) == NXT_OK) {
        return lhq.value;
    }

    return NULL;
}
