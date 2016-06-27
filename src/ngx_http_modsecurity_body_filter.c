/*
 * ModSecurity connector for nginx, http://www.modsecurity.org/
 * Copyright (c) 2015 Trustwave Holdings, Inc. (http://www.trustwave.com/)
 *
 * You may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * If any of the files related to licensing are missing or if you have any
 * other questions related to licensing please contact Trustwave Holdings, Inc.
 * directly using the email address security@modsecurity.org.
 *
 */

#ifndef DDEBUG
#define DDEBUG 0
#endif
#include "ddebug.h"

#include "ngx_http_modsecurity_common.h"

static ngx_http_output_body_filter_pt ngx_http_next_body_filter;

/* XXX: check behaviour on few body filters installed */
ngx_int_t
ngx_http_modsecurity_body_filter_init(void)
{
    ngx_http_next_body_filter = ngx_http_top_body_filter;
    ngx_http_top_body_filter = ngx_http_modsecurity_body_filter;

    return NGX_OK;
}

ngx_int_t
ngx_http_modsecurity_body_filter(ngx_http_request_t *r, ngx_chain_t *in)
{
    int buffer_fully_loadead = 0;
    ngx_chain_t *chain = in;
    ngx_http_modsecurity_ctx_t *ctx = NULL;
#ifdef MODSECURITY_SANITY_CHECKS
    ngx_list_part_t *part = &r->headers_out.headers.part;
    ngx_table_elt_t *data = part->elts;
    ngx_uint_t i = 0;
#endif

    if (in == NULL) {
        return ngx_http_next_body_filter(r, in);
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_modsecurity);

    dd("body filter, recovering ctx: %p", ctx);

    if (ctx == NULL) {
        return ngx_http_next_body_filter(r, in);
    }

#ifdef MODSECURITY_SANITY_CHECKS
    /*
     * Identify if there is a header that was not inspected by ModSecurity.
     */
    for (i = 0; ; i++)
    {
        int found = 0;
        ngx_uint_t j = 0;
        ngx_table_elt_t *s1;
        ngx_http_modsecurity_header_t *vals;

        if (i >= part->nelts)
        {
            if (part->next == NULL) {
                break;
            }

            part = part->next;
            data = part->elts;
            i = 0;
        }

        vals = ctx->headers_out->elts;
        s1 = &data[i];

        /*
         * Headers that were inspected by ModSecurity.
         */
        while (j < ctx->headers_out->nelts)
        {
            ngx_str_t *s2 = &vals[j].name;
            ngx_str_t *s3 = &vals[j].value;

            if (s1->key.len == s2->len && ngx_strncmp(s1->key.data, s2->data, s1->key.len) == 0)
            {
                if (s1->value.len == s3->len && ngx_strncmp(s1->value.data, s3->data, s1->value.len) == 0)
                {
                    found = 1;
                    break;
                }
            }
            j++;
        }
        if (!found) {
            dd("header: `%.*s' with value: `%.*s' was not inpected by ModSecurity",
                (int) s1->key.len,
                (const char *) s1->key.data,
                (int) s1->value.len,
                (const char *) s1->value.data);

            return ngx_http_filter_finalize_request(r,
                &ngx_http_modsecurity, NGX_HTTP_INTERNAL_SERVER_ERROR);
        }
    }
#endif

    for (; chain != NULL; chain = chain->next)
    {
/* XXX: chain->buf->last_buf || chain->buf->last_in_chain */
        if (chain->buf->last_buf) {
            buffer_fully_loadead = 1;
        }
    }

    if (buffer_fully_loadead == 1)
    {
        int ret;

        for (chain = in; chain != NULL; chain = chain->next)
        {
            u_char *data = chain->buf->start;

            msc_append_response_body(ctx->modsec_transaction, data, chain->buf->end - data);
            ret = ngx_http_modsecurity_process_intervention(ctx->modsec_transaction, r);
            if (ret > 0) {
                return ngx_http_filter_finalize_request(r,
                    &ngx_http_modsecurity, ret);
            }
        }

        msc_process_response_body(ctx->modsec_transaction);
/* XXX: I don't get how body from modsec being transferred to nginx's buffer.  If so - after adjusting of nginx's
   XXX: body we can proceed to adjust body size (content-length).  see xslt_body_filter() for example */
        ret = ngx_http_modsecurity_process_intervention(ctx->modsec_transaction, r);
        if (ret > 0) {
            return ret;
        }
	else if (ret < 0) {
            return ngx_http_filter_finalize_request(r,
                &ngx_http_modsecurity, NGX_HTTP_INTERNAL_SERVER_ERROR);
        }
    }
    else
    {
        dd("buffer was not fully loaded! ctx: %p", ctx);
    }

/* XXX: xflt_filter() -- return NGX_OK here */
    return ngx_http_next_body_filter(r, in);
}