#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <rp_irc.h>

%%{
machine irc;
write data;
}%%

%%{
machine irc;

action prefix_servername_start {
}

action prefix_servername {
}

action prefix_servername_finish {
}

action prefix_hostmask_start {
}

action hostmask_nickname {
}

action hostmask_user {
}

action hostmask_host {
}

action prefix_hostmask_finish {
}

action message_code_start {
  ctx->msg.code.len = 0;
}

action message_code {
  ctx->msg.code.ptr[ctx->msg.code.len] = fc;
  ctx->msg.code.len++;
}

action message_code_finish {
}

action params_start {
  ctx->msg.params.len = 0;
}

action params {
  ctx->msg.params.ptr[ctx->msg.params.len] = fc;
  ctx->msg.params.len++;
}

action params_1_start {
}

action params_2_start {
}

action params_1 {
}

action params_2 {
}

action params_1_finish {
}

action params_2_finish {
}

SPACE          = " ";
special        = "[" | "\\" | "]" | "^" | "_" | "`" | "{" | "|" | "}" | "+";
nospcrlfcl     = extend - ( 0 | SPACE | '\r' | '\n' | ':' );
crlf           = "\r\n";
shortname      = ( alnum ( alnum | "-" )* alnum* ) | "*";
multihostname  = shortname ( ( "." | "/" ) shortname )*;
singlehostname = shortname ( "." | "/" );
hostname       = multihostname | singlehostname;
servername     = hostname;
nickname       = ( alpha | special ) ( alnum | special | "-" ){,15};
user           = (extend - ( 0 | "\n" | "\r" | SPACE | "@" ))+;
ip4addr        = digit{1,3} "." digit{1,3} "." digit{1,3} "." digit{1,3};
ip6addr        = ( xdigit+ ( ":" xdigit+ ){7} ) | ( "0:0:0:0:0:" ( "0" | "FFFF"i ) ":" ip4addr );
hostaddr       = ip4addr | ip6addr;
host           = hostname | hostaddr;
hostmask       = nickname $ hostmask_nickname ( ( "!" user $ hostmask_user )? "@" host $ hostmask_host )?;
prefix         = ( servername $ prefix_servername > prefix_servername_start % prefix_servername_finish ) | ( hostmask > prefix_hostmask_start % prefix_hostmask_finish );
code           = alpha+ | digit{3};
middle         = nospcrlfcl ( ":" | nospcrlfcl )*;
trailing       = ( ":" | " " | nospcrlfcl )*;
params_1       = ( SPACE middle $ params_1 > params_1_start ){,14} ( SPACE ":"  trailing $ params_1 > params_1_start )?;
params_2       = ( SPACE middle $ params_2 > params_2_start ){14}  ( SPACE ":"? trailing $ params_2 > params_2_start )?;
params         =  ( params_1 % params_1_finish | params_2 % params_2_finish ) $ params > params_start;
message        = ( ":" prefix SPACE )? ( code $ message_code > message_code_start % message_code_finish ) params? crlf @ { fbreak; };

main := message;
}%%

int rp_irc_csstart = %%{ write start; }%%;

int
rp_irc_parse(struct rp_irc_ctx *ctx, const char *src, size_t *len)
{
  int cs = ctx->cs;

  const char *p = src;
  const char *pe = (const char *)((uintptr_t)src + *len);

  %%write init nocs;
  %%write exec;

  ctx->cs = cs;

  *len = *len - (uintptr_t)(pe - p);

  if (cs >= %%{ write first_final; }%%) {
    ctx->cs = %%{ write start; }%%;
    return 1;
  }

  return 0;
}

