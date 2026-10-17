import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;
import java.io.*;

// Usage: -postScript Disasm8.java <outFile> <start>x<end> ...   (hex, no 0x)
// Prints instructions in each range; for ranges not covered by instructions prints block name + 16-byte rows.
public class Disasm8 extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        AddressSpace sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        Listing lst = currentProgram.getListing();
        Memory mem = currentProgram.getMemory();
        try (PrintWriter w = new PrintWriter(new File(a[0]))) {
            for (int i = 1; i < a.length; i++) {
                String[] p = a[i].split("x");
                Address s = sp.getAddress(Long.parseUnsignedLong(p[0], 16));
                Address e = sp.getAddress(Long.parseUnsignedLong(p[1], 16));
                MemoryBlock b = mem.getBlock(s);
                w.println("== " + a[i] + " block " + (b == null ? "?" : b.getName()));
                Address cur = s;
                while (cur.compareTo(e) < 0) {
                    Instruction ins = lst.getInstructionAt(cur);
                    if (ins != null) {
                        String refs = "";
                        for (Reference r : ins.getReferencesFrom()) refs += " ->" + r.getToAddress();
                        w.println("  " + cur + "  " + ins + refs);
                        cur = ins.getMaxAddress().add(1);
                    } else {
                        Data dd = lst.getDataAt(cur);
                        StringBuilder sb = new StringBuilder();
                        for (int k = 0; k < 16; k++) {
                            try { sb.append(String.format("%02x ", mem.getByte(cur.add(k)) & 0xff)); } catch (Exception ex) { sb.append("?? "); }
                        }
                        Symbol sy = getSymbolAt(cur);
                        w.println("  " + cur + "  " + sb + (dd != null ? " data:" + dd.getDataType().getName() + "=" + dd.getDefaultValueRepresentation() : "") + (sy != null ? " sym:" + sy.getName() : ""));
                        cur = cur.add(16);
                    }
                }
            }
        }
    }
}
