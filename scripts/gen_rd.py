#!/usr/bin/env python
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2019, Linaro Limited
#

from __future__ import print_function

import argparse
import sys
from elftools.elf.elffile import ELFFile
from elftools.elf.sections import SymbolTableSection
import struct
import re
from collections import deque

def va_to_section(elffile, va):
    for section in elffile.iter_sections():
        if section['sh_type'] == 'SHT_PROGBITS' and \
                va >= section['sh_addr'] and \
                va < section['sh_addr'] + section['sh_size']:
            return section

    return None

def read_cstr(elffile, va):
    sect = va_to_section(elffile, va)
    if sect is None:
        return None

    data = sect.data()
    offs = va - sect['sh_addr']
    cstr = ""
    for cnt in range(100):
        s = struct.unpack_from('<s', data, offs + cnt)
        if s[0] == '\x00':
            break
        cstr = cstr + s[0]

    return cstr

def print_reg(outf, name, name_str, base, size, attr, imp_def_attr):
    outf.write("\n")
    outf.write("\t\t%s {\n" % name)
    outf.write("\t\t\tstr = \"%s\";\n" % name_str)
    outf.write("\t\t\tbase = <0x0 0x%x>;\n" % base)
    outf.write("\t\t\tsize = <0x0 0x%x>;\n" % size)
    outf.write("\t\t\tattr = <0x%x>;\n" % attr)
    outf.write("\t\t\timp_def_attr = <0x%x>;\n" % imp_def_attr)
    outf.write("\t\t};\n")

def get_mem_reg(elffile, sym):
    sect = va_to_section(elffile, sym['st_value'])
    if sect is None:
        return

    data = sect.data()
    offs = sym['st_value'] - sect['sh_addr']
    if sym['st_size'] == 32:
        pmem = struct.unpack_from('<QIIQQ', data, offs)
        pmem_name = pmem[0]
        pmem_type = pmem[1]
        pmem_addr = pmem[3]
        pmem_size = pmem[4]
    else:
        print("Unknown struct core_mmu_phys_mem size %d" % sym['st_size'])
        sys.exit(1)

    pmem_name_str = read_cstr(elffile, pmem_name)

    return (sym.name, pmem_name_str, pmem_type, pmem_addr, pmem_size)

def get_memory_regions(elffile):
    symtab = elffile.get_section_by_name('.symtab')
    if not symtab:
        print("Symbol table not found")
        sys.exit(1)

    if not isinstance(symtab, SymbolTableSection):
        print(".symtab not a symbol table")
        sys.exit(1)


    mem_reg = deque()
    for sym in symtab.iter_symbols():
        if re.match('^__scattered_array_.*phys_mem_map$', sym.name):
            mem_reg.append(get_mem_reg(elffile, sym))

    return mem_reg

def print_mem_reg(outf, mem_reg, pmem_type, mr_str, mr_attr):
    for mr in mem_reg:
        if mr[2] == pmem_type and mr[4] > 0:
            print_reg(outf, mr_str, mr_str, mr[3], mr[4], mr_attr, pmem_type)
            return

def print_mem_reg_multi(outf, mem_reg, pmem_type, mr_str, mr_attr):
    c = 0
    for mr in mem_reg:
        if mr[2] == pmem_type:
            print_reg(outf, mr_str + str(c), mr_str, mr[3], mr[4], mr_attr,
                      pmem_type)
            c = c + 1

def emit_memory_regions(outf, mem_reg):
    outf.write("\tmemory_regions {\n")

    memtypes = dict(MEM_AREA_TEE_RAM_RX=2,
                    MEM_AREA_TEE_RAM_RO=3,
                    MEM_AREA_TEE_RAM_RW=4,
                    MEM_AREA_TA_RAM=8,
                    MEM_AREA_RAM_SEC=13,
                    MEM_AREA_IO_SEC=15,
                    MEM_AREA_NSEC_SHM=9)
    attr = dict(RD_MEM_DEVICE=0,
                RD_MEM_NORMAL_CODE=1,
                RD_MEM_NORMAL_DATA=2,
                RD_MEM_NORMAL_BSS=3,
                RD_MEM_NORMAL_RODATA=4,
                RD_MEM_NORMAL_SPM_SP_SHARED_MEM=5,
                RD_MEM_NORMAL_CLIENT_SHARED_MEM=6,
                RD_MEM_NORMAL_MISCELLANEOUS=7)

    print_mem_reg(outf, mem_reg, memtypes['MEM_AREA_TEE_RAM_RX'],
                  "TEE_RAM_RX", attr['RD_MEM_NORMAL_CODE'])
    print_mem_reg(outf, mem_reg, memtypes['MEM_AREA_TEE_RAM_RO'],
                  "TEE_RAM_RO", attr['RD_MEM_NORMAL_RODATA'])
    print_mem_reg(outf, mem_reg, memtypes['MEM_AREA_TEE_RAM_RW'],
                  "TEE_RAM_RW", attr['RD_MEM_NORMAL_DATA'])
    print_mem_reg(outf, mem_reg, memtypes['MEM_AREA_TA_RAM'],
                  "TA_RAM", attr['RD_MEM_NORMAL_DATA'])
    print_mem_reg(outf, mem_reg, memtypes['MEM_AREA_RAM_SEC'],
                  "RAM_SEC", attr['RD_MEM_NORMAL_DATA'])
    print_mem_reg_multi(outf, mem_reg, memtypes['MEM_AREA_IO_SEC'],
                        "IO_SEC", attr['RD_MEM_DEVICE'])
    print_mem_reg(outf, mem_reg, memtypes['MEM_AREA_NSEC_SHM'],
                  "NSEC_SHM", attr['RD_MEM_NORMAL_CLIENT_SHARED_MEM'])

    outf.write("\t};\n")

def emit_attribute(outf, elffile):
    for seg in elffile.iter_segments():
        if seg['p_type'] == 'PT_LOAD':
            load_address = seg['p_vaddr']

    if load_address is None:
        print("Cannot find load address")
        sys.exit(1)

    outf.write("\tattribute {\n")
    outf.write("\t\tversion = <0x1>;\n")
    outf.write("\t\tsp_type = <0x1>;\n")
    outf.write("\t\tpe_mpidr = <0x0>;\n")
    outf.write("\t\truntime_el = <0x1>;\n")
    outf.write("\t\texec_type = <0x1>;\n")
    outf.write("\t\tpanic_policy = <0x1>;\n")
    outf.write("\t\txlat_granule = <0x0>;\n")
    outf.write("\t\tbinary_size = <0x0>;\n")
    outf.write("\t\tload_address = <0x0 0x%x>;\n" % load_address)
    outf.write("\t\tentrypoint = <0x0 0x%x>;\n" % elffile.header['e_entry'])
    outf.write("\t};\n")
    outf.write("\n")

def get_args():
    parser = argparse.ArgumentParser()

    parser.add_argument('--tee_elf',
                        required=True,
                        help='The input tee.elf')

    parser.add_argument('--out',
                        required=False, type=argparse.FileType('wb'),
                        help='The output tee_rd.dts')

    return parser.parse_args()


def main():
    args = get_args()
    inf = open(args.tee_elf, 'rb')
    elffile = ELFFile(inf)

    outf = args.out;

    mem_reg = get_memory_regions(elffile)

    outf.write("/dts-v1/;\n")
    outf.write("\n")
    outf.write("/ {\n")
    outf.write("\tcompatible = \"arm,sp_rd\";\n")
    outf.write("\n")

    emit_attribute(outf, elffile)
    emit_memory_regions(outf, mem_reg)

    outf.write("\n")
    outf.write("\tnotifications {\n")
    outf.write("\n")
    outf.write("\t\tnotification_0 {\n")
    outf.write("\t\t\tattr = <0x0>;\n")
    outf.write("\t\t\tpe = <0x0>;\n")
    outf.write("\t\t};\n")
    outf.write("\t};\n")
    outf.write("};\n")

    inf.close()
    outf.close()

if __name__ == "__main__":
    main()
