import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;
import java.io.*;

// Usage: -postScript StrRefs8.java <outFile> <string> ...
// Finds each NUL-terminated ASCII string, then for every reference to it prints either the
// surrounding instructions (code ref) or 48 bytes of the surrounding data (data ref).
public class StrRefs8 extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        Memory mem = currentProgram.getMemory();
        Listing lst = currentProgram.getListing();
        ReferenceManager rm = currentProgram.getReferenceManager();
        try (PrintWriter w = new PrintWriter(new File(a[0]))) {
            for (int i = 1; i < a.length; i++) {
                byte[] pat = (a[i] + "\0").getBytes("US-ASCII");
                byte[] full = new byte[pat.length + 1];
                System.arraycopy(pat, 0, full, 1, pat.length);
                Address start = currentProgram.getMinAddress();
                w.println("== " + a[i]);
                while (true) {
                    Address h = mem.findBytes(start, full, null, true, monitor);
                    if (h == null) break;
                    Address sa = h.add(1);
                    start = h.add(1);
                    w.println(" str @" + sa);
                    for (Reference r : rm.getReferencesTo(sa)) {
                        Address f = r.getFromAddress();
                        Instruction ins = lst.getInstructionAt(f);
                        if (ins != null) {
                            Instruction p = ins;
                            for (int k = 0; k < 4 && p.getPrevious() != null; k++) p = p.getPrevious();
                            for (int k = 0; k < 9 && p != null; k++, p = p.getNext()) w.println("   " + p.getAddress() + " " + p);
                            w.println("   --");
                        } else {
                            StringBuilder sb = new StringBuilder();
                            Address d0 = f.subtract(16);
                            for (int k = 0; k < 48; k++) sb.append(String.format("%02x%s", mem.getByte(d0.add(k)) & 0xff, k % 8 == 7 ? " | " : " "));
                            w.println("   data ref " + f + " : [" + d0 + "] " + sb);
                        }
                    }
                }
            }
        }
    }
}
