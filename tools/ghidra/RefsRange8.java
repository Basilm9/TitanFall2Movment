import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;
import java.io.*;

// Usage: -postScript RefsRange8.java <outFile> <start>x<len> ...  (hex)
// Lists every reference into [start, start+len), plus raw 8-byte pointer and 4-byte RVA hits for start.
public class RefsRange8 extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        AddressSpace sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        ReferenceManager rm = currentProgram.getReferenceManager();
        Memory mem = currentProgram.getMemory();
        Listing lst = currentProgram.getListing();
        long base = currentProgram.getImageBase().getOffset();
        try (PrintWriter w = new PrintWriter(new File(a[0]))) {
            for (int i = 1; i < a.length; i++) {
                String[] p = a[i].split("x");
                long s = Long.parseUnsignedLong(p[0], 16), n = Long.parseUnsignedLong(p[1], 16);
                w.println("== " + a[i]);
                for (long o = 0; o < n; o++) {
                    Address ta = sp.getAddress(s + o);
                    for (Reference r : rm.getReferencesTo(ta)) {
                        Function f = getFunctionContaining(r.getFromAddress());
                        Instruction ins = lst.getInstructionAt(r.getFromAddress());
                        MemoryBlock b = mem.getBlock(r.getFromAddress());
                        w.println("  +" + Long.toHexString(o) + " from " + r.getFromAddress() + " [" + (b == null ? "?" : b.getName()) + "] " + r.getReferenceType() + " in " + (f == null ? "-" : f.getEntryPoint().toString()) + " : " + (ins == null ? "" : ins.toString()));
                    }
                }
                byte[] p8 = new byte[8];
                for (int k = 0; k < 8; k++) p8[k] = (byte) (s >>> (8 * k));
                long rva = s - base;
                byte[] p4 = new byte[4];
                for (int k = 0; k < 4; k++) p4[k] = (byte) (rva >>> (8 * k));
                for (byte[] pat : new byte[][] { p8, p4 }) {
                    for (MemoryBlock b : mem.getBlocks()) {
                        if (!b.isInitialized()) continue;
                        Address c = b.getStart();
                        int cnt = 0;
                        while (c != null && c.compareTo(b.getEnd()) <= 0 && cnt < 50) {
                            Address h = mem.findBytes(c, b.getEnd(), pat, null, true, monitor);
                            if (h == null) break;
                            w.println("  raw" + pat.length + " " + h + " [" + b.getName() + "]");
                            cnt++;
                            c = h.add(1);
                        }
                    }
                }
            }
        }
    }
}
