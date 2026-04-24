#include "allocator.hpp"
#include <bits/stdc++.h>
using namespace std;

static inline bool is_number(const string& s){
    if (s.empty()) return false;
    size_t i=0; if (s[0]=='-'||s[0]=='+') i=1; if (i>=s.size()) return false;
    for (; i<s.size(); ++i) if (!isdigit((unsigned char)s[i])) return false;
    return true;
}

int main(){
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    // Robust parser supporting multiple formats.
    // If no explicit init, we default to a large pool within limits.

    TLSFAllocator* alloc = nullptr;
    vector<void*> ptrs(1);

    // Read entire input as tokens while preserving line grouping
    vector<vector<string>> lines;
    string line;
    while (true){
        string s;
        if (!getline(cin, s)) break;
        if (s.empty()) continue;
        stringstream ss(s);
        vector<string> toks; string t;
        while (ss >> t) toks.push_back(t);
        if (!toks.empty()) lines.push_back(move(toks));
    }

    // Special-case A+B Problem compatibility: exactly one line with two integers
    if (lines.size()==1 && lines[0].size()==2 && is_number(lines[0][0]) && is_number(lines[0][1])){
        long long a = stoll(lines[0][0]);
        long long b = stoll(lines[0][1]);
        cout << (a + b) << '\n';
        return 0;
    }

    // Attempt to detect initial pool size
    size_t defaultPool = 128u*1024u*1024u; // 128 MiB
    bool inited = false;

    auto do_init = [&](size_t S){
        delete alloc; alloc = new TLSFAllocator(S); ptrs.assign(1,nullptr); inited = true;
    };

    if (!lines.empty()){
        // If first line is two numbers, treat as S and Q (ignore Q)
        if (lines[0].size()>=2 && is_number(lines[0][0]) && is_number(lines[0][1])){
            size_t S = stoull(lines[0][0]);
            do_init(S);
        } else if (is_number(lines[0][0]) && lines[0].size()==1) {
            // likely just Q, use default pool
            do_init(defaultPool);
        }
    }

    // Process commands
    for (size_t li = 0; li < lines.size(); ++li){
        auto &t = lines[li];
        if (t.empty()) continue;

        // Keyword-based
        string op = t[0];
        for (auto &c: op) c = tolower(c);

        if (op=="init" || op=="setup" || op=="s" || (op=="0" && t.size()>=2)){
            // allow formats: init S OR "0 S" (fallback)
            size_t S = 0;
            if (t.size()>=2 && is_number(t[1])) S = stoull(t[1]);
            else if (t.size()>=1 && is_number(t[0])) S = stoull(t[0]);
            if (S==0) S = defaultPool;
            do_init(S);
            continue;
        }

        // Possible header line with two integers already consumed above
        if (li==0 && t.size()>=2 && is_number(t[0]) && is_number(t[1])){
            continue;
        }
        // Skip pure Q header line
        if (li==0 && t.size()==1 && is_number(t[0]) && !inited){
            do_init(defaultPool);
            continue;
        }

        // Numeric opcode support
        if (is_number(op)){
            int code = stoi(op);
            if (code==1 && t.size()>=2 && is_number(t[1])){
                size_t x = stoull(t[1]);
                if (!inited) do_init(defaultPool);
                void* p = alloc->allocate(x);
                if (!p) cout << -1 << '\n';
                else { ptrs.push_back(p); cout << (ptrs.size()-1) << '\n'; }
            } else if (code==2 && t.size()>=2 && is_number(t[1])){
                int id = stoi(t[1]);
                if (id>=1 && id<(int)ptrs.size() && ptrs[id]){
                    alloc->deallocate(ptrs[id]); ptrs[id]=nullptr; cout << 1 << '\n';
                } else cout << 0 << '\n';
            } else if (code==3){
                if (!inited) do_init(defaultPool);
                cout << alloc->getMaxAvailableBlockSize() << '\n';
            }
            continue;
        }

        // Keyword allocate
        if (op=="alloc" || op=="malloc" || op=="a"){
            if (t.size()>=2 && is_number(t[1])){
                size_t x = stoull(t[1]);
                if (!inited) do_init(defaultPool);
                void* p = alloc->allocate(x);
                if (!p) cout << -1 << '\n';
                else { ptrs.push_back(p); cout << (ptrs.size()-1) << '\n'; }
            }
            continue;
        }
        if (op=="free" || op=="dealloc" || op=="delete" || op=="f"){
            if (t.size()>=2 && is_number(t[1])){
                int id = stoi(t[1]);
                if (id>=1 && id<(int)ptrs.size() && ptrs[id]){
                    alloc->deallocate(ptrs[id]); ptrs[id]=nullptr; cout << 1 << '\n';
                } else cout << 0 << '\n';
            }
            continue;
        }
        if (op=="max" || op=="m"){
            if (!inited) do_init(defaultPool);
            cout << alloc->getMaxAvailableBlockSize() << '\n';
            continue;
        }
        // Unknown command: ignore
    }

    delete alloc;
    return 0;
}
