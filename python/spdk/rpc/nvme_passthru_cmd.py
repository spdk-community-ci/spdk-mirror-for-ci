#  SPDX-License-Identifier: BSD-3-Clause
#  Copyright (C) 2014-2024 Beijing Memblaze Technology Co., Ltd.
#  All rights reserved.

import os
import base64
from ctypes import c_uint64, c_uint32, c_uint16, c_uint8, c_char
from ctypes import Structure, addressof, sizeof, string_at
from ctypes import create_string_buffer, memmove

from .nvme import bdev_nvme_send_cmd


def encode_urlsafe_base64(data: bytes) -> str:
    """Encode bytes using the URL- and filesystem-safe Base64 alphabet.

    Args:
        data: Argument data is a bytes-like object to encode.

    Returns:
        The result is returned as a bytes object.
        The alphabet uses '-' instead of '+' and '_' instead of '/'.
    """
    return base64.urlsafe_b64encode(data).decode(encoding='utf-8', errors='replace')


def decode_urlsafe_base64(data: bytes) -> bytes:
    """Decode bytes using the URL- and filesystem-safe Base64 alphabet.

    Args:
        data: Argument data is a bytes-like object or ASCII string to decode.

    Returns:
        The result is returned as a bytes object. A binascii.Error is raised if the input
        is incorrectly padded.  Characters that are not in the URL-safe base-64
        alphabet, and are not a plus '+' or slash '/', are discarded prior to the
        padding check.

    The alphabet uses '-' instead of '+' and '_' instead of '/'.
    """
    return base64.urlsafe_b64decode(data)


def read_binary_file(file_path):
    """Function to generate a binary data from a file

    Args:
        file_path: File path
    Returns:
        Function returns binary data
    """
    binary_data = None
    try:
        with open(file_path, "rb") as file:
            binary_data = file.read()
    except FileNotFoundError:
        print(f"{file_path} does not exist")
    except Exception as e:
        print(f"read {file_path} ERROR: {e}")
    return binary_data


def write_binary_file(file_path, data: bytes):
    """Function writes the bytes object data to a file

    Args:
        file_path: File path
        data:  Argument data is a bytes-like object
    """
    try:
        with open(file_path, "wb") as file:
            file.write(data)
    except FileNotFoundError:
        print(f"{file_path} does not exist")
    except Exception as e:
        print(f"read {file_path} ERROR: {e}")


def hex_int(value: str) -> int:
    """Function converts a string decimal or hexadecimal number into an integer

    Args:
        value: The value is a string in decimal or hexadecimal
    Returns:
        Returns an integer
    """
    if value.lower().startswith("0x"):
        return int(value, 16)
    else:
        return int(value)


def output_data(dataDict: dict, file=None):
    """Displays the dictionary data returned by the rpc server in hexadecimal.
        It only displays data whose key is' data 'in the dictionary
        If the file argument is passed,data is written to the specified file.

    Args:
        dataDict: Dictionary data returned by the rpc server.
                  The dictionary data is the 'NVMe completion queue entry, requested data and metadata,
                  all are encoded by base64 urlsafe.'
        file: Specifies the file to write to.
    """
    data = dataDict.get("data", None)
    if data:
        cBytesStr = decode_urlsafe_base64(data)
        if file:
            write_binary_file(file, cBytesStr)
        else:
            cString = ""
            s = "buffer:\t 0  1  2  3  4  5  6  7  8  9  10 11 12 13 14 15"
            for i in range(len(cBytesStr)):
                if i % 16 == 0:
                    if 31 < cBytesStr[i] < 127:
                        cString += chr(cBytesStr[i])
                    else:
                        cString += "."
                    s = s + "\n%04x\t" % i + " %02x" % cBytesStr[i]
                else:
                    if 31 < cBytesStr[i] < 127:
                        cString = cString + chr(cBytesStr[i])
                    else:
                        cString = cString + "."
                    s = s + " %02x" % cBytesStr[i]
                if len(cString) == 16:
                    s = s + "\t" + cString
                    cString = ""

            s = s + "\t" + cString
            print(s)


def print_cpl(dataDict: dict):
    """The NVMe completion queue data is displayed

    Args:
        dataDict: Dictionary data returned by the rpc server.
                  The dictionary data is the 'NVMe completion queue entry, requested data and metadata,
                  all are encoded by base64 urlsafe.'
    """
    cplData = dataDict.get("cpl", None)
    if cplData:
        raw_cpl = decode_urlsafe_base64(cplData)
        cpl = Completion()
        cpl.set_raw_str_custom(0, 16, raw_cpl)
        print(f"NVMe status: NDR: {cpl.dnr}, M: {cpl.more}, CRD: {cpl.crd}, SCT: {cpl.sct}, SC:0x{cpl.sc:02x}")
        return cpl.sct << 8 | cpl.sc
    else:
        print(f"Unknown error: {dataDict}")
        return None


def bdev_nvme_admin_passthru(client, name, opcode, fuse=0, rsvd=0, nsid=0, cdw2=0, cdw3=0,
                             cdw10=0, cdw11=0, cdw12=0, cdw13=0, cdw14=0, cdw15=0,
                             read=None, write=None,
                             data=None, metadata=None,
                             data_len=None, metadata_len=None,
                             timeout_ms=None,
                             show=True):
    """Send one NVMe admin command

    Args:
        name: Name of the operating NVMe controller
        opcode: NVMe admin command opcode
        fuse: Fused operation
        rsvd: Value for reserved field
        nsid: Namespace id
        cdw2: Command dword 2 value
        cdw3: Command dword 3 value
        cdw10: Command dword 10 value
        cdw11: Command dword 11 value
        cdw12: Command dword 12 value
        cdw13: Command dword 13 value
        cdw14: Command dword 14 value
        cdw15: Command dword 15 value
        read: Set dataflow direction to receive(controller to host)
        write: Set dataflow direction to send(host to controller)
        data: Data transferring to controller from host. (file, string, bytes).
        metadata: metadata transferring to controller from host.(file, string or bytes)
        data_length: Data length required to transfer from controller to host
        metadata_length: Metadata length required to transfer from controller to host
        timeout-ms: Command execution timeout value, in milliseconds, if 0, don't track timeout

        show: Print the returned data if set to True

    Returns:
        NVMe completion queue entry, requested data and metadata.
    """

    admin_cmd = NvmeCommand()
    admin_cmd.opc = opcode
    admin_cmd.fuse = 0 if fuse is None else fuse
    admin_cmd.rsvd = 0 if rsvd is None else rsvd
    admin_cmd.nsid = 0 if nsid is None else nsid
    admin_cmd.cdw2 = 0 if cdw2 is None else cdw2
    admin_cmd.cdw3 = 0 if cdw3 is None else cdw3
    admin_cmd.cdw10 = 0 if cdw10 is None else cdw10
    admin_cmd.cdw11 = 0 if cdw11 is None else cdw11
    admin_cmd.cdw12 = 0 if cdw12 is None else cdw12
    admin_cmd.cdw13 = 0 if cdw13 is None else cdw13
    admin_cmd.cdw14 = 0 if cdw14 is None else cdw14
    admin_cmd.cdw15 = 0 if cdw15 is None else cdw15
    cmdbuf = admin_cmd.to_base64_urlsafe()

    data_direction = "c2h"
    if write:
        data_direction = "h2c"
        if data:
            if isinstance(data, bytes):
                bytes_data = data
            elif os.path.isfile(data):
                bytes_data = read_binary_file(data)
            else:
                bytes_data = data.encode(encoding='utf-8', errors='replace')
            if bytes_data is None:
                return None
            data = encode_urlsafe_base64(bytes_data)
            data_len = len(bytes_data) if data_len is None else data_len

        if metadata:
            if isinstance(metadata, bytes):
                bytes_data = metadata
            elif os.path.isfile(metadata):
                bytes_data = read_binary_file(metadata)
            else:
                bytes_data = data.encode(encoding='utf-8', errors='replace')
            if bytes_data is None:
                return None

            metadata = encode_urlsafe_base64(bytes_data)
            metadata_len = len(bytes_data) if metadata_len is None else metadata_len

    dataDict = bdev_nvme_send_cmd(client, name, "admin", data_direction, cmdbuf,
                                  data=data, metadata=metadata,
                                  data_len=data_len, metadata_len=metadata_len,
                                  timeout_ms=timeout_ms)
    if show:
        status = print_cpl(dataDict)
        if status == 0:
            output_data(dataDict, data)
    return dataDict


def bdev_nvme_firmware_slot_info(client, name):
    """Send one Firmware Slot Information command (Opcode 02h; Log Page Identifier 03h)

    Args:
        name: Name of the operating NVMe controller. Example: Nvme0

    Print:
        Firmware Slot Information.
    """
    opcode = 0x02  # Get Log Page
    data_len = 512  # Data length of firmware slot information
    lid = 0x03  # Log Page ID: Firmware Slot Information
    numd = (data_len >> 2) - 1  # dword number (0's based).
    cdw10 = ((numd & 0xffff) << 16) | (lid & 0xff)
    dataDict = bdev_nvme_admin_passthru(client, name, opcode=opcode, fuse=0, rsvd=0, nsid=0,
                                        cdw2=0, cdw3=0,
                                        cdw10=cdw10, cdw11=0, cdw12=0, cdw13=0, cdw14=0, cdw15=0,
                                        read=None, write=None,
                                        data=None, metadata=None,
                                        data_len=data_len, metadata_len=None,
                                        timeout_ms=None,
                                        show=False)
    status = print_cpl(dataDict)
    if status == 0:
        data = dataDict.get("data", None)
        if data:
            cBytesStr = decode_urlsafe_base64(data)
            fw_slot_info = FirmwareSlotInformation()
            fw_slot_info.set_raw_str_custom(0, data_len, cBytesStr)
        print(fw_slot_info)


def bdev_nvme_firmware_download(client, name, filename, xfer=None, offset=0):
    """Send one Firmware Image Download command

    Args:
        name: Name of the operating NVMe controller. Example: Nvme0
        filename: path of the nvme firmware file.
        xfer: transfer chunksize limit. The default transfer size is 4096.
              (A size larger than 8192 is not recommended; An exception occurs when rpc is
               passed from client to server)
        offset: starting dword offset, default 0.(When there are multiple image files,
                the offset parameter may be needed)

    Print:
        Firmware Slot Information.
    """
    opcode = 0x11  # Firmware Image Download
    xfer = 4096 if xfer is None else xfer
    offset = 0 if offset is None else offset

    fw_data_bytes = read_binary_file(filename)
    if fw_data_bytes is None:
        return
    remain_file_size_in_bytes = len(fw_data_bytes)
    offset_from_data_in_bytes = 0
    while (remain_file_size_in_bytes > 0):
        if remain_file_size_in_bytes > xfer:
            transfer_size_in_dwords = xfer >> 2  # number of dwords for each transfer
        else:
            transfer_size_in_dwords = remain_file_size_in_bytes >> 2

        numd = transfer_size_in_dwords - 1     # 0's based value
        ofst = (offset_from_data_in_bytes + offset) >> 2
        transfer_size_in_Bytes = transfer_size_in_dwords << 2
        data = fw_data_bytes[offset_from_data_in_bytes: offset_from_data_in_bytes + transfer_size_in_Bytes]
        dataDict = bdev_nvme_admin_passthru(client, name, opcode=opcode, fuse=0, rsvd=0, nsid=0,
                                            cdw2=0, cdw3=0,
                                            cdw10=numd, cdw11=ofst, cdw12=0, cdw13=0, cdw14=0, cdw15=0,
                                            read=None, write=True,
                                            data=data, metadata=None,
                                            data_len=transfer_size_in_Bytes, metadata_len=None,
                                            timeout_ms=None,
                                            show=False)
        cplData = dataDict.get("cpl", None)
        if cplData:
            raw_cpl = decode_urlsafe_base64(cplData)
            cpl = Completion()
            cpl.set_raw_str_custom(0, 16, raw_cpl)
            if (cpl.sct << 8 | cpl.sc) != 0:
                if cpl.sct == 0x01 and cpl.sc == 0x14:
                    print("\tFirmware download error: Overlapping Range.")
                break
        else:
            print(f"Unknown error: {dataDict}")
            break

        remain_file_size_in_bytes -= transfer_size_in_Bytes
        offset_from_data_in_bytes += transfer_size_in_Bytes
    print("Firmware download success")


def bdev_nvme_firmware_commit(client, name, ca=0, fs=0, bpid=0):
    """Send one Firmware commit command

    Args:
        name: Name of the operating NVMe controller. Example: Nvme0
        ca: [0-7]: commit action
        fs: [0-7]: firmware slot for commit action
        bpid: [0, 1] boot partition id - Specifies the Boot Partition that shall be used for
                     the Commit Action, if applicable (default: 0).

    Print:
        Display firmware commit command specific status values.
    """
    opcode = 0x10  # Firmware Commit
    ca = 0 if ca is None else ca
    fs = 0 if fs is None else fs
    bpid = 0 if bpid is None else bpid

    cdw10 = ((bpid & 0x01) << 31) | ((ca & 0x07) << 3) | (fs & 0x07)
    dataDict = bdev_nvme_admin_passthru(client, name, opcode=opcode, fuse=0, rsvd=0, nsid=0,
                                        cdw2=0, cdw3=0,
                                        cdw10=cdw10, cdw11=0, cdw12=0, cdw13=0, cdw14=0, cdw15=0,
                                        read=None, write=None,
                                        data=None, metadata=None,
                                        data_len=None, metadata_len=None,
                                        timeout_ms=None,
                                        show=False)

    status = print_cpl(dataDict)
    if status == 0:
        print(f"\tSuccess committing firmware action:{ca} slot:{fs}")
    elif (status >> 8 == 1) and (status & 0xff == 0x06):
        print(f"\tFirmware action:{ca} slot:{fs}. "
              f"Invalid Firmware Slot: {fs}")
    elif (status >> 8 == 1) and (status & 0xff == 0x07):
        print(f"\tFirmware action:{ca} slot:{fs}. "
              "Invalid Firmware Image.")
    elif (status >> 8 == 1) and (status & 0xff == 0x0b):
        print(f"\tSuccess committing firmware action:{ca} slot:{fs}, "
              "buf firmware activation requires conventional reset.")
    elif (status >> 8 == 1) and (status & 0xff == 0x10):
        print(f"\tSuccess committing firmware action:{ca} slot:{fs}, "
              "buf firmware activation requires nvm subsystem reset.")
    elif (status >> 8 == 1) and (status & 0xff == 0x11):
        print(f"\tSuccess committing firmware action:{ca} slot:{fs}, "
              "buf firmware activation requires controller level reset.")
    elif (status >> 8 == 1) and (status & 0xff == 0x12):
        print(f"\tFirmware action:{ca} slot:{fs}. "
              "Firmware Activation Requires Maximum Time Violation.")
    elif (status >> 8 == 1) and (status & 0xff == 0x13):
        print(f"\tFirmware action:{ca} slot:{fs}. "
              "Firmware Activation Prohibited.")
    elif (status >> 8 == 1) and (status & 0xff == 0x14):
        print(f"\tFirmware action:{ca} slot:{fs}. "
              "Overlapping Range.")
    elif (status >> 8 == 1) and (status & 0xff == 0x1e):
        print(f"\tFirmware action:{ca} slot:{fs}. "
              "Boot Partition Write Prohibited.")
    else:
        print(f"\tFirmware action:{ca} slot:{fs}. "
              f"Unknown NVMe status: 0x{status:0x}")


class StructureBase(Structure):
    """
    The base class of the command structure
    """
    _pack_ = 1
    _fields_ = []

    def to_bytes_str(self, offset=0, size=None) -> bytes:
        """Return the byte string from command structure data. If size is specified, it is used as size,
           otherwise the string is assumed to be zero-terminated.

        Args:
            offset: The offset of the command structure. Unit byte
            size: If size is specified, it is used as size,
                  otherwise the string is assumed to be zero-terminated. Unit byte
        Returns:
            Return the byte string from command structure data.
        """
        assert addressof(self) - offset > 0, "Offset %d overflow" % offset
        if size is not None:
            assert sizeof(self) - size > 0, "Size %d overflow" % offset
        else:
            size = 0
        return string_at(addressof(self) + offset, sizeof(self) - size)

    def to_str(self, offset=0, size=None, encoding='utf-8', errors='replace') -> str:
        """Return the string from command structure data.

        Args:
            offset: The offset of the command structure. Unit byte
            size: If size is specified, it is used as size,
                  otherwise the string is assumed to be zero-terminated. Unit byte
            encoding: The encoding with which to decode the bytes.
            errors: The error handling scheme to use for the handling of decoding errors.
                    The 'strict' meaning that decoding errors raise a
                    UnicodeDecodeError. Other possible values are 'ignore' and 'replace'
                    as well as any other name registered with codecs.register_error that
                    can handle UnicodeDecodeErrors.
        Returns:
            Return the string from command structure data.
        """
        bytesStr = self.to_bytes_str(offset=offset, size=size)
        return bytesStr.decode(encoding=encoding, errors=errors)

    def to_base64_urlsafe(self, offset=0, size=None) -> bytes:
        """Encode command structure data using the URL- and filesystem-safe Base64 alphabet.

        Args:
            offset: The offset of the command structure. Unit byte
            size: If size is specified, it is used as size,
                  otherwise the string is assumed to be zero-terminated. Unit byte

        Returns:
            The result is returned as a bytes object.
            The alphabet uses '-' instead of '+' and '_' instead of '/'.
        """
        return encode_urlsafe_base64(self.to_bytes_str(
            offset=offset, size=size))

    def set_raw_str_custom(self, offset: int, num: int, bytesStr: bytes):
        """Populates the bytes string into the specified position in the command structure based on
           the values of offset and num

        Args:
            offset: The offset of the command structure. Unit byte
            num: the length of a bytes string

        offset plus num Exceeds the size of the command structure. An exception is raised.
        """
        if offset >= sizeof(self):
            raise Exception("offset > total buffer size")
        if offset + num > sizeof(self):
            size = sizeof(self) - offset
        else:
            size = num
        if len(bytesStr) > size:
            raise Exception("The length of bytesStr is greater than num")
        buf = create_string_buffer(size)
        buf.raw = bytesStr
        memmove(addressof(self) + offset, buf, sizeof(buf))


class NvmeCommand(StructureBase):
    """
    The class of admin command dw0 to dw15 structure
    """
    _pack_ = 1
    _fields_ = [
        # func-name, type , bit
        # Command Dword0
        ("opc", c_uint16, 8),       # opcode
        ("fuse", c_uint16, 2),      # fused operation
        ("rsvd", c_uint16, 4),     # Reserved
        ("psdt", c_uint16, 2),      # PRP or SGL for Data Transfer
        ("cid", c_uint16),

        # Command Dword1
        ("nsid", c_uint32),         # namespace identifier

        # Command Dword2
        ("cdw2", c_uint32),        # Reserved

        # Command Dword3
        ("cdw3", c_uint32),        # Reserved

        # Command Dword4 Dword5
        ("mptr", c_uint64),         # metadata pointer

        # Command Dword6 Dword7
        ("prp1", c_uint64),         # prp entry 1

        # Command Dword8 Dword9
        ("prp2", c_uint64),         # prp entry 2

        # Command Dword10
        ("cdw10", c_uint32),

        # Command Dword11
        ("cdw11", c_uint32),

        # Command Dword12
        ("cdw12", c_uint32),

        # Command Dword13
        ("cdw13", c_uint32),

        # Command Dword14
        ("cdw14", c_uint32),

        # Command Dword15
        ("cdw15", c_uint32),
    ]


class Completion(StructureBase):
    """
    The class of NVMe completion queue entry structure
    """
    _pack_ = 1
    _fields_ = [
        # func-name, type , bit
        ("cdw0", c_uint32),         # Completion Queue dw0
        ("cdw1", c_uint32),         # Completion Queue dw1
        ("sqhp", c_uint16),         # SQ Head Pointer
        ("sqid", c_uint16),         # SQ Identifier
        ("cid", c_uint32, 16),      # Command Identifier
        ("ptag", c_uint32, 1),      # Phase Tag
        ("sc", c_uint32, 8),        # Status Code
        ("sct", c_uint32, 3),       # Status Code Type
        ("crd", c_uint32, 2),       # Command Retry Delay
        ("more", c_uint32, 1),      # More
        ("dnr", c_uint32, 1),       # Do Not Retry
    ]

    def __str__(self):
        """Displays completion queue tntry structure members and their data
        """
        header = "Completion Queue Entry:"
        body = ""
        for func_array in self._fields_:
            func = func_array[0]
            body += "\t%s\t:0x%x\n" % (func, getattr(self, func, 0))

        s = "%s\n%s" % (header, body)
        return s


class FirmwareSlotInformation(StructureBase):
    """
    The class of firmware slot information structure
    """
    _pack_ = 1
    _fields_ = [
        # func-name, type
        ("afi", c_uint8),
        ("rsvd", c_uint8 * 7),
        ("frs1", c_char * 8),
        ("frs2", c_char * 8),
        ("frs3", c_char * 8),
        ("frs4", c_char * 8),
        ("frs5", c_char * 8),
        ("frs6", c_char * 8),
        ("frs7", c_char * 8),
        ("rsvd1", c_uint8 * 448),
    ]

    def __str__(self):
        """Displays firmware slot information structure members and their data
        """
        header = "Firmware Slot Information Log Page:"
        body = ""
        for func_array in self._fields_:
            func = func_array[0]
            if func == "afi":
                body += "%s\t: 0x%x" % (func, getattr(self, func, 0))
            elif "frs" in func:
                if getattr(self, func, b""):
                    body += f"\n{func}\t: 0x"
                    for i in range(8):
                        try:
                            body += f"{getattr(self, func)[7 - i]:x}"
                        except IndexError:
                            body += "00"
                    body += f" ({getattr(self, func).decode(encoding='utf-8', errors='replace')})"

        s = "%s\n%s" % (header, body)
        return s
