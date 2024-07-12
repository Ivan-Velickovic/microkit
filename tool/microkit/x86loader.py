from pathlib import Path
from struct import pack
from typing import Optional, List, Tuple
from io import BytesIO

from microkit.elf import ElfFile, ElfSegment, SegmentAttributes, MachineType
from microkit.util import round_up, MemoryRegion
from microkit.sel4 import KernelConfig

class X86Loader:
    """Special loader for X86.

    On X86 we take advantage of the multiboot bootloader that can load
    all ELF sections at their physical addresses. We inject extra
    sections to the loader ELF file for the PDs, the kernel, and the
    initial task. The output image file is a 32-bit ELF file because
    grub does not load 64-bit ELF files in legacy (BIOS) boot.

    """

    def __init__(self,
        kernel_config: KernelConfig,
        loader_elf_path: Path,
        kernel_elf: ElfFile,
        initial_task_elf: ElfFile,
        initial_task_phys_base: Optional[int],
        reserved_region: MemoryRegion,
        regions: List[Tuple[int, bytes]],
    ) -> None:
        # Load the loader ELF file.
        self._elf = ElfFile.from_path(loader_elf_path)

        # Add the PD memory regions as segments.
        for r in regions:
            segment = ElfSegment(phys_addr=r[0],
                                 virt_addr=0,
                                 data=r[1],
                                 loadable=True,
                                 attrs=SegmentAttributes.PF_R)
            self._elf.add_segment(segment)

        # Add the kernel memory regions as segments.
        for segment in kernel_elf.segments:
            # Wipe the virtual address fields that are unnecessary and
            # cause issues since they are 64-bit wide.
            segment.virt_addr = 0
            self._elf.add_segment(segment)

        # Save the kernel's entry point address so we can jump into it
        # once we're done with our boot dance.
        self._elf.write_symbol("kernel_entry", pack('<I', kernel_elf.entry))

        # Save the address and size of the reserved memory region that
        # holds the PD regions and the monitor invocation table.
        self._elf.write_symbol("extra_device_addr_p", pack('<Q', reserved_region.base))
        self._elf.write_symbol("extra_device_size", pack('<Q', reserved_region.size))

        # Export the monitor task as a binary ELF64 file in memory.
        monitor_raw = BytesIO()
        initial_task_elf.iowrite(monitor_raw, MachineType.EM_X86_64)

        # Add the monitor ELF file as a segment to the loader, and
        # have it loaded at a page aligned address just after the
        # loader.
        bss_end, _ = self._elf.find_symbol("_bss_end")
        monitor_addr = round_up(bss_end, 0x1000)
        monitor_size = len(monitor_raw.getbuffer())
        monitor_segment = ElfSegment(phys_addr=monitor_addr,
                                     virt_addr=0,
                                     data=monitor_raw.getvalue(),
                                     loadable=True,
                                     attrs=SegmentAttributes.PF_R)
        self._elf.add_segment(monitor_segment)

        # Save the monitor's loaded address and size.
        self._elf.write_symbol("monitor_addr", pack('<I', monitor_addr))
        self._elf.write_symbol("monitor_size", pack('<I', monitor_size))

    def write_image(self, path: Path) -> None:
        self._elf.write(path, MachineType.EM_386)
