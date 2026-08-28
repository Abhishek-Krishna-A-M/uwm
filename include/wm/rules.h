#ifndef RULES_H
#define RULES_H

struct uwm_config;
struct uwm_toplevel;

void rule_apply_all(struct uwm_config *config, struct uwm_toplevel *toplevel);

#endif
