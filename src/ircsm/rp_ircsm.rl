#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <rp_ircsm.h>

%%{
machine irc;
write data;
}%%

%%{
machine irc;

action msg_start {
  msg->is_hostmask = 0;
  msg->is_servername = 0;
}

action prefix_start {
  msg->prefix.len = 0;
}

action prefix {
  msg->prefix.ptr[msg->prefix.len++] = fc;
}

action prefix_servername_start {
  msg->servername.ptr = &msg->prefix.ptr[0];
  msg->servername.len = 0;
}

action prefix_servername {
  msg->servername.len++;
}

action prefix_servername_finish {
  msg->is_servername = 1;
}

action prefix_hostmask_start {
  msg->hostmask.user.len = 0;
  msg->hostmask.host.len = 0;
}

action hostmask_nickname_start {
  msg->hostmask.nick.len = 0;
  msg->hostmask.nick.ptr = &msg->prefix.ptr[msg->prefix.len];
}

action hostmask_nickname {
  msg->hostmask.nick.len++;
}

action hostmask_user_start {
  msg->hostmask.user.len = 0;
  msg->hostmask.user.ptr = &msg->prefix.ptr[msg->prefix.len];
}

action hostmask_user {
  msg->hostmask.user.len++;
}

action hostmask_host_start {
  msg->hostmask.host.len = 0;
  msg->hostmask.host.ptr = &msg->prefix.ptr[msg->prefix.len];
}

action hostmask_host {
  msg->hostmask.host.len++;
}

action prefix_hostmask_finish {
  msg->is_hostmask = 1;
}

action message_code_start {
  msg->code.len = 0;
}

action message_code {
  msg->code.ptr[msg->code.len++] = fc;
}

action message_code_finish {
}

action params_start {
  msg->params.len = 0;
}

action params {
  if (!(msg->params.len == 0 && fc == ' ')) {
    msg->params.ptr[msg->params.len++] = fc;
  }
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
hostmask       = nickname $ hostmask_nickname > hostmask_nickname_start ( ( "!" user $ hostmask_user > hostmask_user_start )? "@" host $ hostmask_host > hostmask_host_start )?;
prefix         = ( servername $ prefix_servername > prefix_servername_start % prefix_servername_finish ) | ( hostmask > prefix_hostmask_start % prefix_hostmask_finish );
code           = alpha+ | digit{3};
middle         = nospcrlfcl ( ":" | nospcrlfcl )*;
trailing       = ( ":" | " " | nospcrlfcl )*;
params_1       = ( SPACE middle $ params_1 > params_1_start ){,14} ( SPACE ":"  trailing $ params_1 > params_1_start )?;
params_2       = ( SPACE middle $ params_2 > params_2_start ){14}  ( SPACE ":"? trailing $ params_2 > params_2_start )?;
params         = ( params_1 % params_1_finish | params_2 % params_2_finish ) $ params > params_start;
message        = (( ":" prefix $ prefix > prefix_start SPACE )? ( code $ message_code > message_code_start % message_code_finish ) params? crlf @ { fbreak; }) > msg_start;

main := message;
}%%

int
rp_ircsm_init(int *state)
{
  *state = %%{ write start; }%%;

  return 0;
}

int
rp_ircsm_msg_init(rp_pool_t *pool, struct rp_ircsm_msg *msg)
{
  memset(msg, 0, sizeof(*msg));
  msg->prefix.ptr = rp_palloc(pool, 64);
  msg->code.ptr = rp_palloc(pool, 32);
  msg->params.ptr = rp_palloc(pool, 512);

  return 0;
}

int
rp_ircsm_parse(struct rp_ircsm_msg *msg, int *state, const char *src, size_t *len)
{
  int cs = *state;
  const char *p = src;
  const char *pe = (const char *)((uintptr_t)p + *len);

  %%write init nocs;
  %%write exec;

  *state = cs;
  *len = *len - (uintptr_t)(pe - p);

  if (cs >= %%{ write first_final; }%%) {
    *state = %%{ write start; }%%;
    return 1;
  }

  return 0;
}

