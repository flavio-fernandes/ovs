/*
 * Copyright (c) 2015 Nicira, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/in.h>
#include <linux/ip.h>
#include <linux/net.h>
#include <linux/rculist.h>
#include <linux/udp.h>
#include <linux/if_vlan.h>
#include <linux/module.h>

#include <net/lisp.h>
#include <net/icmp.h>
#include <net/ip.h>
#include <net/route.h>
#include <net/udp.h>
#include <net/xfrm.h>

#include "datapath.h"
#include "vport.h"
#include "vport-netdev.h"

static struct vport_ops ovs_lisp_vport_ops;
/**
 * struct lisp_port - Keeps track of open UDP ports
 * @dst_port: destination port.
 */
struct lisp_port {
	u16 port_no;
};

static inline struct lisp_port *lisp_vport(const struct vport *vport)
{
	return vport_priv(vport);
}

static int lisp_get_options(const struct vport *vport,
			      struct sk_buff *skb)
{
	struct lisp_port *lisp_port = lisp_vport(vport);

	if (nla_put_u16(skb, OVS_TUNNEL_ATTR_DST_PORT, lisp_port->port_no))
		return -EMSGSIZE;
	return 0;
}

static struct vport *lisp_tnl_create(const struct vport_parms *parms)
{
	struct net *net = ovs_dp_get_net(parms->dp);
	struct nlattr *options = parms->options;
	struct lisp_port *lisp_port;
	struct net_device *dev;
	struct vport *vport;
	struct nlattr *a;
	u16 dst_port;
	int err;

	if (!options) {
		err = -EINVAL;
		goto error;
	}

	a = nla_find_nested(options, OVS_TUNNEL_ATTR_DST_PORT);
	if (a && nla_len(a) == sizeof(u16)) {
		dst_port = nla_get_u16(a);
	} else {
		/* Require destination port from userspace. */
		err = -EINVAL;
		goto error;
	}

	vport = ovs_vport_alloc(sizeof(struct lisp_port),
				&ovs_lisp_vport_ops, parms);
	if (IS_ERR(vport))
		return vport;

	lisp_port = lisp_vport(vport);
	lisp_port->port_no = dst_port;

	rtnl_lock();
	dev = lisp_dev_create_fb(net, parms->name, NET_NAME_USER, dst_port);
	if (IS_ERR(dev)) {
		rtnl_unlock();
		ovs_vport_free(vport);
		return ERR_CAST(dev);
	}

	dev_change_flags(dev, dev->flags | IFF_UP);
	rtnl_unlock();
	return vport;
error:
	return ERR_PTR(err);
}

static struct vport *lisp_create(const struct vport_parms *parms)
{
	struct vport *vport;

	vport = lisp_tnl_create(parms);
	if (IS_ERR(vport))
		return vport;

	return ovs_netdev_link(vport, parms->name);
}

static struct vport_ops ovs_lisp_vport_ops = {
	.type		= OVS_VPORT_TYPE_LISP,
	.create		= lisp_create,
	.destroy	= ovs_netdev_tunnel_destroy,
	.get_options	= lisp_get_options,
#ifndef USE_UPSTREAM_TUNNEL
	.fill_metadata_dst = lisp_fill_metadata_dst,
#endif
	.send		= lisp_xmit,
};

static int __init ovs_lisp_tnl_init(void)
{
	return ovs_vport_ops_register(&ovs_lisp_vport_ops);
}

static void __exit ovs_lisp_tnl_exit(void)
{
	ovs_vport_ops_unregister(&ovs_lisp_vport_ops);
}

module_init(ovs_lisp_tnl_init);
module_exit(ovs_lisp_tnl_exit);

MODULE_DESCRIPTION("OVS: Lisp switching port");
MODULE_LICENSE("GPL");
MODULE_ALIAS("vport-type-105");
