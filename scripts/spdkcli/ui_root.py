from .ui_node import UINode, UIBdevs, UILvolStores, UIVhosts
import rpc.client
import rpc
from functools import wraps


class UIRoot(UINode):
    """
    Root node for CLI menu tree structure. Refreshes running config on startup.
    """
    def __init__(self, s, shell):
        UINode.__init__(self, "/", shell=shell)
        self.current_bdevs = []
        self.current_lvol_stores = []
        self.current_vhost_ctrls = []
        self.set_rpc_target(s)
        self.verbose = False

    def refresh(self):
        self._children = set([])
        UIBdevs(self)
        UILvolStores(self)
        UIVhosts(self)

    def set_rpc_target(self, s):
        self.client = rpc.client.JSONRPCClient(s)

    def print_array(self, a):
        return " ".join(a)

    def verbose(f):
        # For any configuration calls (create, delete, construct, etc.)
        # Check if verbose option is to be used and set appropriately.
        # Do not use for "get_*" methods so that output is not
        # flooded.
        def w(self, **kwargs):
            self.client.verbose = self.verbose
            r = f(self, **kwargs)
            self.client.verbose = False
            return r
        return w

    def get_bdevs(self, bdev_type):
        self.current_bdevs = rpc.bdev.get_bdevs(self.client)
        # Following replace needs to be done in order for some of the bdev
        # listings to work: logical volumes, split disk.
        # For example logical volumes: listing in menu is "Logical_Volume"
        # (cannot have space), but the product name in SPDK is "Logical Volume"
        bdev_type = bdev_type.replace("_", " ")
        for bdev in filter(lambda x: bdev_type in x["product_name"].lower(),
                           self.current_bdevs):
            test = Bdev(bdev)
            yield test

    def get_bdevs_iostat(self, **kwargs):
        return rpc.bdev.get_bdevs_iostat(self.client, **kwargs)

    @verbose
    def split_bdev(self, **kwargs):
        response = rpc.bdev.construct_split_vbdev(self.client, **kwargs)
        return self.print_array(response)

    @verbose
    def destruct_split_bdev(self, **kwargs):
        rpc.bdev.destruct_split_vbdev(self.client, **kwargs)

    @verbose
    def delete_bdev(self, name):
        rpc.bdev.delete_bdev(self.client, bdev_name=name)

    @verbose
    def create_malloc_bdev(self, **kwargs):
        response = rpc.bdev.construct_malloc_bdev(self.client, **kwargs)
        return response

    @verbose
    def delete_malloc_bdev(self, **kwargs):
        rpc.bdev.delete_malloc_bdev(self.client, **kwargs)

    @verbose
    def create_iscsi_bdev(self, **kwargs):
        response = rpc.bdev.construct_iscsi_bdev(self.client, **kwargs)
        return response

    @verbose
    def delete_iscsi_bdev(self, **kwargs):
        rpc.bdev.delete_iscsi_bdev(self.client, **kwargs)

    @verbose
    def create_aio_bdev(self, **kwargs):
        response = rpc.bdev.construct_aio_bdev(self.client, **kwargs)
        return response

    @verbose
    def delete_aio_bdev(self, **kwargs):
        rpc.bdev.delete_aio_bdev(self.client, **kwargs)

    @verbose
    def create_lvol_bdev(self, **kwargs):
        response = rpc.lvol.construct_lvol_bdev(self.client, **kwargs)
        return response

    @verbose
    def create_nvme_bdev(self, **kwargs):
        response = rpc.bdev.construct_nvme_bdev(self.client, **kwargs)
        return response

    @verbose
    def delete_nvme_controller(self, **kwargs):
        rpc.bdev.delete_nvme_controller(self.client, **kwargs)

    @verbose
    def create_null_bdev(self, **kwargs):
        response = rpc.bdev.construct_null_bdev(self.client, **kwargs)
        return response

    @verbose
    def delete_null_bdev(self, **kwargs):
        rpc.bdev.delete_null_bdev(self.client, **kwargs)

    @verbose
    def create_error_bdev(self, **kwargs):
        response = rpc.bdev.construct_error_bdev(self.client, **kwargs)

    @verbose
    def delete_error_bdev(self, **kwargs):
        rpc.bdev.delete_error_bdev(self.client, **kwargs)

    def get_lvol_stores(self):
        self.current_lvol_stores = rpc.lvol.get_lvol_stores(self.client)
        for lvs in self.current_lvol_stores:
            yield LvolStore(lvs)

    @verbose
    def create_lvol_store(self, **kwargs):
        response = rpc.lvol.construct_lvol_store(self.client, **kwargs)
        return response

    @verbose
    def delete_lvol_store(self, **kwargs):
        rpc.lvol.destroy_lvol_store(self.client, **kwargs)

    @verbose
    def create_pmem_pool(self, **kwargs):
        response = rpc.pmem.create_pmem_pool(self.client, **kwargs)
        return response

    @verbose
    def delete_pmem_pool(self, **kwargs):
        rpc.pmem.delete_pmem_pool(self.client, **kwargs)

    @verbose
    def create_pmem_bdev(self, **kwargs):
        response = rpc.bdev.construct_pmem_bdev(self.client, **kwargs)
        return response

    @verbose
    def create_rbd_bdev(self, **kwargs):
        response = rpc.bdev.construct_rbd_bdev(self.client, **kwargs)
        return response

    @verbose
    def create_virtio_dev(self, **kwargs):
        response = rpc.vhost.construct_virtio_dev(self.client, **kwargs)
        return self.print_array(response)

    @verbose
    def remove_virtio_scsi_bdev(self, **kwargs):
        response = rpc.vhost.remove_virtio_scsi_bdev(self.client, **kwargs)
        return response

    def get_virtio_scsi_devs(self):
        for bdev in rpc.vhost.get_virtio_scsi_devs(self.client):
            test = Bdev(bdev)
            yield test

    def list_vhost_ctrls(self):
        self.current_vhost_ctrls = rpc.vhost.get_vhost_controllers(self.client)

    def get_vhost_ctrlrs(self, ctrlr_type):
        self.list_vhost_ctrls()
        for ctrlr in filter(lambda x: ctrlr_type in x["backend_specific"].keys(),
                            self.current_vhost_ctrls):
            yield VhostCtrlr(ctrlr)

    @verbose
    def remove_vhost_controller(self, **kwargs):
        rpc.vhost.remove_vhost_controller(self.client, **kwargs)

    @verbose
    def create_vhost_scsi_controller(self, **kwargs):
        rpc.vhost.construct_vhost_scsi_controller(self.client, **kwargs)

    @verbose
    def create_vhost_blk_controller(self, **kwargs):
        rpc.vhost.construct_vhost_blk_controller(self.client, **kwargs)

    @verbose
    def remove_vhost_scsi_target(self, **kwargs):
        rpc.vhost.remove_vhost_scsi_target(self.client, **kwargs)

    @verbose
    def add_vhost_scsi_lun(self, **kwargs):
        rpc.vhost.add_vhost_scsi_lun(self.client, **kwargs)

    def set_vhost_controller_coalescing(self, **kwargs):
        rpc.vhost.set_vhost_controller_coalescing(self.client, **kwargs)


class Bdev(object):
    def __init__(self, bdev_info):
        """
        All class attributes are set based on what information is received
        from get_bdevs RPC call.
        # TODO: Document in docstring parameters which describe bdevs.
        # TODO: Possible improvement: JSON schema might be used here in future
        """
        for i in bdev_info.keys():
            setattr(self, i, bdev_info[i])


class LvolStore(object):
    def __init__(self, lvs_info):
        """
        All class attributes are set based on what information is received
        from get_bdevs RPC call.
        # TODO: Document in docstring parameters which describe bdevs.
        # TODO: Possible improvement: JSON schema might be used here in future
        """
        for i in lvs_info.keys():
            setattr(self, i, lvs_info[i])


class VhostCtrlr(object):
    def __init__(self, ctrlr_info):
        """
        All class attributes are set based on what information is received
        from get_vhost_controllers RPC call.
        # TODO: Document in docstring parameters which describe bdevs.
        # TODO: Possible improvement: JSON schema might be used here in future
        """
        for i in ctrlr_info.keys():
            setattr(self, i, ctrlr_info[i])
