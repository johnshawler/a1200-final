; data.s - the NES ROMs, made by tools/make_assets.sh. Read by the CPU only, so any memory will do.
        section romdata,data
        xdef    _nes_prg_data,_nes_chr_data
        cnop    0,4
_nes_prg_data:  incbin  "assets/prg.bin"
        cnop    0,4
_nes_chr_data:  incbin  "assets/chr.bin"
