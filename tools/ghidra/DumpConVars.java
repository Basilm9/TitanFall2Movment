import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;
import java.util.regex.*;

// Usage: -postScript DumpConVars.java <out.tsv> <ctorAddr> [ctorAddr...]
// Decompiles every caller of the ConVar constructors and writes name, default, flags, help (DAT_ pointers resolved to strings).
public class DumpConVars extends GhidraScript {
    String readCString(Address a) {
        try {
            StringBuilder sb = new StringBuilder();
            Memory m = currentProgram.getMemory();
            for (int i = 0; i < 512; i++) {
                byte b = m.getByte(a.add(i));
                if (b == 0) break;
                sb.append((char) (b & 0xff));
            }
            return sb.toString();
        } catch (Exception e) { return "?"; }
    }

    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        AddressFactory af = currentProgram.getAddressFactory();
        Pattern dat = Pattern.compile("&?DAT_([0-9a-f]{8,16})");
        Set<Function> callers = new LinkedHashSet<>();
        for (int i = 1; i < a.length; i++) {
            Address ctor = af.getAddress(a[i]);
            for (Reference r : getReferencesTo(ctor)) {
                Function f = getFunctionContaining(r.getFromAddress());
                if (f != null) callers.add(f);
            }
        }
        println("callers: " + callers.size());
        try (PrintWriter w = new PrintWriter(new File(a[0]), "UTF-8")) {
            for (Function f : callers) {
                DecompileResults res = di.decompileFunction(f, 60, monitor);
                if (!res.decompileCompleted()) continue;
                String c = res.getDecompiledFunction().getC();
                for (String line : c.split("\n")) {
                    boolean isCtor = false;
                    for (int i = 1; i < a.length; i++) if (line.contains("FUN_" + a[i].replace("0x", "").toLowerCase() + "(")) isCtor = true;
                    if (!isCtor) continue;
                    // skip the first DAT_ (the ConVar object itself), resolve the rest as strings
                    Matcher m = dat.matcher(line);
                    StringBuffer sb = new StringBuffer();
                    boolean first = true;
                    while (m.find()) {
                        if (first) { first = false; m.appendReplacement(sb, Matcher.quoteReplacement(m.group())); continue; }
                        String s = readCString(af.getAddress("0x" + m.group(1)));
                        m.appendReplacement(sb, Matcher.quoteReplacement("\"" + s + "\""));
                    }
                    m.appendTail(sb);
                    w.println(f.getEntryPoint() + "\t" + sb.toString().trim());
                }
            }
        }
    }
}
