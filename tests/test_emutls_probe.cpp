thread_local int g_tls_val = 123;
int main() {
    g_tls_val += 1;
    return g_tls_val;
}
