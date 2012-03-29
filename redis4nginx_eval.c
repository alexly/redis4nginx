#ifndef DDEBUG
#define DDEBUG 0
#endif

#include "ddebug.h"
#include "redis4nginx_module.h"
#include "redis4nginx_adapter.h"

ngx_int_t redis4nginx_eval_handler(ngx_http_request_t *r)
{     
    redis4nginx_loc_conf_t *loc_conf;
    redis4nginx_srv_conf_t *serv_conf;
    ngx_uint_t i, argv_count;
    ngx_http_complex_value_t *compiled_values;
    ngx_str_t value;
    char **argv;
    size_t *argvlen;
    
    serv_conf = ngx_http_get_module_srv_conf(r, redis4nginx_module);
    
    loc_conf = ngx_http_get_module_loc_conf(r, redis4nginx_module);

    // connect to redis db, only if connection is lost
    if(redis4nginx_init_connection(&serv_conf->host, serv_conf->port) != NGX_OK)
        return NGX_HTTP_GATEWAY_TIME_OUT;
    
    // we response to 'GET' and 'HEAD' requests only 
    if (!(r->method & NGX_HTTP_GET))
        return NGX_HTTP_NOT_ALLOWED;

    argv_count = loc_conf->queries->nelts;
    compiled_values = loc_conf->queries->elts;
    
    argv = ngx_palloc(r->pool, sizeof(const char *) * argv_count);
    argvlen = ngx_palloc(r->pool, sizeof(size_t) * argv_count);
    
    for (i = 0; i <= argv_count - 1; i++) {
        if (ngx_http_complex_value(r, &compiled_values[i], &value) != NGX_OK)
            return NGX_ERROR;
        
        argv[i] = (char *)value.data;
        argvlen[i] = value.len;
    }
        
    redis4nginx_async_command_argv(redis4nginx_eval_callback, r, argv_count, (const char**)argv, argvlen);
    
    r->main->count++;
    
    return NGX_DONE;
}

void redis4nginx_eval_callback(redisAsyncContext *c, void *repl, void *privdata)
{
    ngx_http_request_t *r = privdata;
    redisReply* rr = repl;
    
    redis4nginx_send_json(r, c, rr);
}

char *redis4nginx_eval_handler_init(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    redis4nginx_loc_conf_t *loc_conf = conf;
    ngx_http_core_loc_conf_t *core_conf;
    ngx_str_t                          *value;
    ngx_uint_t                          i, args_count;
    ngx_http_complex_value_t           *cv;
    ngx_http_compile_complex_value_t    ccv;

	core_conf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
	core_conf->handler = &redis4nginx_eval_handler;
    
    args_count = cf->args->nelts - 1;
    value = cf->args->elts;
    
    loc_conf->queries = ngx_array_create(cf->pool, args_count, sizeof(ngx_http_complex_value_t));

    for (i = 1; i < cf->args->nelts; i++) {
        cv = ngx_array_push(loc_conf->queries);
        if (cv == NULL) {
            return NGX_CONF_ERROR;
        }

        ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));

        ccv.cf = cf;
        ccv.value = &value[i];
        ccv.complex_value = cv;

        if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
            return NGX_CONF_ERROR;
        }
    }

    return NGX_CONF_OK;
    
}
