
def gen_unsafe(size):
    print """template <%s>
T * tpie_unsafe_new(%s) {
   allocation_scope_magic<T> m; 
#ifdef WIN32
#pragma warning(push)
#pragma warning(disable: 4345)
#endif
   new(m.allocate()) T(%s); 
#ifdef WIN32
#pragma warning(pop)
#endif
   return m.finalize();
}
"""%(", ".join(["typename T"]+["typename T%d"%i for i in range(size)]), 
     ", ".join(["T%d t%d"%(i,i) for i in range(size)]), 
     ", ".join(["t%d"%i for i in range(size)]))
      
def gen_interface(argc, argv):
    L=["typename T"] + ["typename T%d"%i for i in range(argc)]
    M=[]
    N=["T"]
    for i in range(argc):
        if (1 << i) & argv:
            M.append("const T%d& t%d"%(i,i))
            N.append("const T%d&"%(i))
        else:
            M.append("T%d& t%d"%(i,i))
            N.append("T%d&"%(i))
    O=["t%d"%i for i in range(argc)]
    print "template <%s> T * tpie_new(%s) {return tpie_unsafe_new<%s>(%s);}"%(", ".join(L), ", ".join(M), ", ".join(N), ", ".join(O))
    

if __name__ == "__main__":
    print "//This file is auto generated, do not edit"
    print    
    for i in range(20):
        gen_unsafe(i)

    for i in range(9):
        for j in range(2**i):
            gen_interface(i, j)
    
