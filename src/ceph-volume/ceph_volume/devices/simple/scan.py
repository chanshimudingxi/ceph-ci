from __future__ import print_function
import argparse
import base64
import json
import logging
import os
from textwrap import dedent
from ceph_volume import decorators, terminal, conf
from ceph_volume.api import lvm
from ceph_volume.util import arg_validators, system, disk, encryption


logger = logging.getLogger(__name__)


class Scan(object):

    help = 'Capture metadata from an OSD data partition or directory'

    def __init__(self, argv):
        self.argv = argv
        self._etc_path = '/etc/ceph/osd/'

    @property
    def etc_path(self):
        if os.path.isdir(self._etc_path):
            return self._etc_path

        if not os.path.exists(self._etc_path):
            os.mkdir(self._etc_path)
            return self._etc_path

        error = "OSD Configuration path (%s) needs to be a directory" % self._etc_path
        raise RuntimeError(error)

    def get_contents(self, path):
        with open(path, 'r') as fp:
            contents = fp.readlines()
        if len(contents) > 1:
            return ''.join(contents)
        return ''.join(contents).strip().strip('\n')

    def scan_device(self, path):
        device_metadata = {'path': None, 'uuid': None}
        if not path:
            return device_metadata
        if self.is_encrypted:
            encryption_metadata = encryption.legacy_encrypted(path)
            device_metadata['path'] = encryption_metadata['device']
            device_metadata['uuid'] = disk.get_partuuid(encryption_metadata['device'])
            return device_metadata
        # cannot read the symlink if this is tmpfs
        if os.path.islink(path):
            device = os.readlink(path)
        else:
            device = path
        lvm_device = lvm.get_lv_from_argument(device)
        if lvm_device:
            device_uuid = lvm_device.lv_uuid
        else:
            device_uuid = disk.get_partuuid(device)

        device_metadata['uuid'] = device_uuid
        device_metadata['path'] = device

        return device_metadata

    def scan_directory(self, path):
        osd_metadata = {'cluster_name': conf.cluster}
        directory_files = os.listdir(path)
        if 'keyring' not in directory_files:
            raise RuntimeError(
                'OSD files not found, required "keyring" file is not present at: %s' % path
            )
        for _file in os.listdir(path):
            file_path = os.path.join(path, _file)
            if os.path.islink(file_path) and os.path.exists(file_path):
                osd_metadata[_file] = self.scan_device(file_path)
            if os.path.isdir(file_path):
                continue
            # the check for binary needs to go before the file, to avoid
            # capturing data from binary files but still be able to capture
            # contents from actual files later
            try:
                if system.is_binary(file_path):
                    continue
            except IOError:
                continue
            if os.path.isfile(file_path):
                content = self.get_contents(file_path)
                try:
                    osd_metadata[_file] = int(content)
                except ValueError:
                    osd_metadata[_file] = content

        # we must scan the paths again because this might be a temporary mount
        path_mounts = system.get_mounts(paths=True)
        device = path_mounts.get(path)
        # it is possible to have more than one device, pick the first one, and
        # warn that it is possible that more than one device is 'data'
        if not device:
            terminal.error('Unable to detect device mounted for path: %s' % path)
            raise RuntimeError('Cannot activate OSD')
        osd_metadata['data'] = self.scan_device(device[0] if len(device) else None)

        return osd_metadata

    def scan_encrypted(self):
        device = self.encryption_metadata['device']
        lockbox = self.encryption_metadata['lockbox']
        encryption_type = self.encryption_metadata['type']
        osd_metadata = {}
        # Get the PARTUUID of the device to make sure have the right one and
        # that maps to the data device
        device_uuid = disk.get_partuuid(device)
        dm_path = '/dev/mapper/%s' % device_uuid
        # check if this partition is already mapped
        device_status = encryption.status(device_uuid)

        # capture all the information from the lockbox first, reusing the
        # directory scan method
        if self.device_mounts.get(lockbox):
            lockbox_path = self.device_mounts.get(lockbox)[0]
            lockbox_metadata = self.scan_directory(lockbox_path)
            # ceph-disk stores the fsid as osd-uuid in the lockbox, thanks ceph-disk
            dmcrypt_secret = encryption.get_dmcrypt_key(
                None,  # There is no ID stored in the lockbox
                lockbox_metadata['osd-uuid'],
                os.path.join(lockbox_path, 'keyring')
            )
        else:
            with system.tmp_mount(lockbox) as lockbox_path:
                lockbox_metadata = self.scan_directory(lockbox_path)
                # ceph-disk stores the fsid as osd-uuid in the lockbox, thanks ceph-disk
                dmcrypt_secret = encryption.get_dmcrypt_key(
                    None,  # There is no ID stored in the lockbox
                    lockbox_metadata['osd-uuid'],
                    os.path.join(lockbox_path, 'keyring')
                )

        if not device_status:
            if encryption_type == 'luks':
                encryption.luks_open(dmcrypt_secret, device, device_uuid)
            else:
                dmcrypt_secret = base64.b64decode(dmcrypt_secret)
                encryption.plain_open(dmcrypt_secret, device, device_uuid)

        # Now check if that mapper is mounted already, to avoid remounting and
        # decrypting the device
        dm_path_mount = self.device_mounts.get(dm_path)
        if dm_path_mount:
            osd_metadata = self.scan_directory(dm_path_mount[0])
        else:
            with system.tmp_mount(dm_path, encrypted=True) as device_path:
                osd_metadata = self.scan_directory(device_path)

        osd_metadata['encrypted'] = True
        osd_metadata['encryption_type'] = encryption_type
        osd_metadata['lockbox.keyring'] = lockbox_metadata['keyring']
        return osd_metadata

    @decorators.needs_root
    def scan(self, args):
        osd_metadata = {'cluster_name': conf.cluster}
        osd_path = None
        logger.info('detecting if argument is a device or a directory: %s', args.osd_path)
        if os.path.isdir(args.osd_path):
            logger.info('will scan directly, path is a directory')
            osd_path = args.osd_path
            mounted_device = system.get_mounts(paths=True).get(args.osd_path)[0]
            # Must re-scan since nothing in the dir can tell us if this is
            # encrypted or what the lockbox may be
            self.encryption_metadata = encryption.legacy_encrypted(mounted_device)
            self.is_encrypted = self.encryption_metadata['encrypted']
        else:
            # assume this is a device, check if it is mounted and use that path
            logger.info('path is not a directory, will check if mounted')
            if system.device_is_mounted(args.osd_path):
                logger.info('argument is a device, which is mounted')
                mounted_osd_paths = self.device_mounts.get(args.osd_path)
                osd_path = mounted_osd_paths[0] if len(mounted_osd_paths) else None

        # argument is not a directory, and it is not a device that is mounted
        # somewhere so temporarily mount it to poke inside, otherwise, scan
        # directly
        if not osd_path:
            # check if we have an encrypted device first, so that we can poke at
            # the lockbox instead
            if self.is_encrypted:
                if not self.encryption_metadata.get('lockbox'):
                    raise RuntimeError(
                        'Lockbox partition was not found for device: %s' % args.osd_path
                    )
                osd_metadata = self.scan_encrypted()
            else:
                logger.info('device is not mounted, will mount it temporarily to scan')
                with system.tmp_mount(args.osd_path) as osd_path:
                    osd_metadata = self.scan_directory(osd_path)
        else:
            logger.info('will scan OSD directory at path: %s', osd_path)
            osd_metadata = self.scan_directory(osd_path)

        osd_id = osd_metadata['whoami']
        osd_fsid = osd_metadata['fsid']
        filename = '%s-%s.json' % (osd_id, osd_fsid)
        json_path = os.path.join(self.etc_path, filename)
        if os.path.exists(json_path) and not args.stdout:
            if not args.force:
                raise RuntimeError(
                    '--force was not used and OSD metadata file exists: %s' % json_path
                )

        if args.stdout:
            print(json.dumps(osd_metadata, indent=4, sort_keys=True, ensure_ascii=False))
        else:
            with open(json_path, 'w') as fp:
                json.dump(osd_metadata, fp, indent=4, sort_keys=True, ensure_ascii=False)
                fp.write(os.linesep)
            terminal.success(
                'OSD %s got scanned and metadata persisted to file: %s' % (
                    osd_id,
                    json_path
                )
            )
            terminal.success(
                'To take over managment of this scanned OSD, and disable ceph-disk and udev, run:'
            )
            terminal.success('    ceph-volume simple activate %s %s' % (osd_id, osd_fsid))

        if not osd_metadata.get('data'):
            msg = 'Unable to determine device mounted on %s' % args.osd_path
            logger.warning(msg)
            terminal.warning(msg)
            terminal.warning('OSD will not be able to start without this information:')
            terminal.warning('    "data": "/path/to/device",')
            logger.warning('Unable to determine device mounted on %s' % args.osd_path)

    def main(self):
        sub_command_help = dedent("""
        Scan an OSD directory for files and configurations that will allow to
        take over the management of the OSD.

        Scanned OSDs will get their configurations stored in
        /etc/ceph/osd/<id>-<fsid>.json

        For an OSD ID of 0 with fsid of ``a9d50838-e823-43d6-b01f-2f8d0a77afc2``
        that could mean a scan command that looks like::

            ceph-volume lvm scan /var/lib/ceph/osd/ceph-0

        Which would store the metadata in a JSON file at::

            /etc/ceph/osd/0-a9d50838-e823-43d6-b01f-2f8d0a77afc2.json

        To a scan an existing, running, OSD:

            ceph-volume simple scan /var/lib/ceph/osd/{cluster}-{osd id}

        And to scan a device (mounted or unmounted) that has OSD data in it, for example /dev/sda1

            ceph-volume simple scan /dev/sda1
        """)
        parser = argparse.ArgumentParser(
            prog='ceph-volume simple scan',
            formatter_class=argparse.RawDescriptionHelpFormatter,
            description=sub_command_help,
        )

        parser.add_argument(
            '-f', '--force',
            action='store_true',
            help='If OSD has already been scanned, the JSON file will be overwritten'
        )

        parser.add_argument(
            '--stdout',
            action='store_true',
            help='Do not save to a file, output metadata to stdout'
        )

        parser.add_argument(
            'osd_path',
            metavar='OSD_PATH',
            type=arg_validators.OSDPath(),
            nargs='?',
            help='Path to an existing OSD directory or OSD data partition'
        )

        if len(self.argv) == 0:
            print(sub_command_help)
            return

        args = parser.parse_args(self.argv)
        if disk.is_partition(args.osd_path):
            label = disk.lsblk(args.osd_path)['PARTLABEL']
            if 'data' not in label:
                raise RuntimeError('Device must be the data partition, but got: %s' % label)

        # Capture some environment status, so that it can be reused all over
        self.device_mounts = system.get_mounts(devices=True)
        self.path_mounts = system.get_mounts(paths=True)
        self.encryption_metadata = encryption.legacy_encrypted(args.osd_path)
        self.is_encrypted = self.encryption_metadata['encrypted']

        self.scan(args)
