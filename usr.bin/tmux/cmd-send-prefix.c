/* $OpenBSD: cmd-send-prefix.c,v 1.9 2012/01/21 08:40:09 nicm Exp $ */

/*
 * Copyright (c) 2007 Nicholas Marriott <nicm@users.sourceforge.net>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER
 * IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>

#include "tmux.h"

/*
 * Send prefix key as a key.
 */

int	cmd_send_prefix_exec(struct cmd *, struct cmd_ctx *);

const struct cmd_entry cmd_send_prefix_entry = {
	"send-prefix", NULL,
	"2t:", 0, 0,
	"[-2] " CMD_TARGET_PANE_USAGE,
	0,
	NULL,
	NULL,
	cmd_send_prefix_exec
};

int
cmd_send_prefix_exec(struct cmd *self, struct cmd_ctx *ctx)
{
	struct args		*args = self->args;
	struct session		*s;
	struct window_pane	*wp;
	int			 key;

	if (cmd_find_pane(ctx, args_get(args, 't'), &s, &wp) == NULL)
		return (-1);

	if (args_has(args, '2'))
		key = options_get_number(&s->options, "prefix2");
	else
		key = options_get_number(&s->options, "prefix");
	window_pane_key(wp, s, key);

	return (0);
}
