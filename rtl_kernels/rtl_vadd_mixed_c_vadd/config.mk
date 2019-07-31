VIVADO := $(XILINX_VIVADO)/bin/vivado
$(TEMP_DIR)/rtl_vadd.xo: src/kernel.xml scripts/package_kernel.tcl scripts/gen_rtl_vadd_xo.tcl src/hdl/*.sv src/hdl/*.v
	mkdir -p $(TEMP_DIR)
	$(VIVADO) -mode batch -source scripts/gen_rtl_vadd_xo.tcl -tclargs $(TEMP_DIR)/rtl_vadd.xo vadd $(TARGET) $(DSA)
$(TEMP_DIR)/krnl_vadd.xo: ./src/krnl_vadd.cpp
	mkdir -p $(TEMP_DIR)
	$(XOCC) $(CLFLAGS) -c -k krnl_vadd -I'$(<D)' -o'$@' '$<' 
