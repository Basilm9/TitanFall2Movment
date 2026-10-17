import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.nio.file.*;
import java.util.*;
import java.util.regex.*;

// Usage: -postScript DumpConVarUsers.java <convars.tsv> <regexFile> <outDir>
// For every ConVar whose name matches the regex, finds functions referencing any address inside the ConVar object
// (obj .. obj+0x90), decompiles them, and rewrites DAT_<addr> references to cv_<name>[+off].
public class DumpConVarUsers extends GhidraScript {
    static final int CV_SIZE = 0x90;

    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        Pattern nameRe = Pattern.compile(new String(Files.readAllBytes(Paths.get(a[1]))).trim(), Pattern.CASE_INSENSITIVE);
        File out = new File(a[2]);
        out.mkdirs();
        AddressFactory af = currentProgram.getAddressFactory();
        Pattern line = Pattern.compile("DAT_([0-9a-f]+),\"([^\"]+)\"");

        TreeMap<Long, String> all = new TreeMap<>();   // every ConVar (for renaming)
        List<long[]> wanted = new ArrayList<>();
        for (String l : Files.readAllLines(Paths.get(a[0]))) {
            Matcher m = line.matcher(l);
            if (!m.find()) continue;
            long obj = Long.parseUnsignedLong(m.group(1), 16);
            all.put(obj, m.group(2));
            if (nameRe.matcher(m.group(2)).find()) wanted.add(new long[]{obj});
        }

        Map<Function, Set<String>> users = new LinkedHashMap<>();
        for (long[] w : wanted) {
            String name = all.get(w[0]);
            for (int off = 0; off < CV_SIZE; off++) {
                Address ad = af.getDefaultAddressSpace().getAddress(w[0] + off);
                for (Reference r : getReferencesTo(ad)) {
                    Function f = getFunctionContaining(r.getFromAddress());
                    if (f == null || f.getBody().getNumAddresses() < 80) continue; // skip ctor/atexit stubs
                    users.computeIfAbsent(f, k -> new TreeSet<>()).add(name);
                }
            }
        }
        println("user functions: " + users.size());

        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        Pattern dat = Pattern.compile("DAT_([0-9a-f]{9,16})");
        try (PrintWriter idx = new PrintWriter(new File(out, "index.txt"))) {
            for (Map.Entry<Function, Set<String>> e : users.entrySet()) {
                Function f = e.getKey();
                DecompileResults res = di.decompileFunction(f, 180, monitor);
                String c = res.decompileCompleted() ? res.getDecompiledFunction().getC() : "// decompile failed: " + res.getErrorMessage();
                Matcher m = dat.matcher(c);
                StringBuffer sb = new StringBuffer();
                while (m.find()) {
                    long ad = Long.parseUnsignedLong(m.group(1), 16);
                    Map.Entry<Long, String> fl = all.floorEntry(ad);
                    String rep = m.group();
                    if (fl != null && ad - fl.getKey() < CV_SIZE) {
                        long off = ad - fl.getKey();
                        rep = "cv_" + fl.getValue() + (off == 0 ? "" : "_0x" + Long.toHexString(off));
                    } else {
                        // annotate read-only constants with their float/double interpretation
                        Address cad = af.getDefaultAddressSpace().getAddress(ad);
                        ghidra.program.model.mem.MemoryBlock b = currentProgram.getMemory().getBlock(cad);
                        if (b != null && b.getName().equals(".rdata")) {
                            try {
                                float fv = Float.intBitsToFloat(currentProgram.getMemory().getInt(cad));
                                double dv = Double.longBitsToDouble(currentProgram.getMemory().getLong(cad));
                                rep = m.group() + "/*f=" + fv + (Math.abs(dv) > 1e-6 && Math.abs(dv) < 1e9 ? " d=" + dv : "") + "*/";
                            } catch (Exception ex) { }
                        }
                    }
                    m.appendReplacement(sb, Matcher.quoteReplacement(rep));
                }
                m.appendTail(sb);
                try (PrintWriter w = new PrintWriter(new File(out, "fn_" + f.getEntryPoint() + ".c"))) { w.println(sb); }
                idx.println(f.getEntryPoint() + "\t" + f.getBody().getNumAddresses() + "\t" + String.join(",", e.getValue()));
            }
        }
    }
}
