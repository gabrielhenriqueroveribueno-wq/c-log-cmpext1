/*
 * ==========================================================================
 *  CONTROLE DE ENTREGAS, RETIRADAS E ESTOQUE - LOCACAO PARA FESTAS
 * --------------------------------------------------------------------------
 *  CMPEXT1 - Projeto de Extensao 1 | TADS-EAD IFSP
 *  Grupo .........: ____________________
 *  Integrantes ...: ____________________
 *  Demandante ....: ____________________ (locacao de mesas e cadeiras)
 *  Finalidade ....: esboco minimamente funcional. Substitui a lousa do
 *                   deposito e a planilha impressa de rotas: registra cada
 *                   pedido (entrega + retirada obrigatoria), controla o
 *                   estoque de todos os itens de locacao, avisa quando falta
 *                   item ou quando o estoque fica abaixo do minimo, monta a
 *                   programacao do dia por rota (A1, A2, B1...) e registra a
 *                   baixa de cada parada.
 *                   Gera um painel.html para ver no navegador e pode ligar
 *                   um servidor na rede local para os motoristas darem
 *                   baixa pelo celular.
 *  Tecnologia ....: C++ estruturado (funcoes + structs), menu de console,
 *                   persistencia em arquivos CSV (separador ';'), sockets
 *                   (Winsock no Windows, POSIX no Linux/Mac).
 *  Compilar ......: Windows (MinGW/Dev-C++): g++ -std=c++11 -o controle
 *                     controle_entregas_retiradas_estoque.cpp -lws2_32
 *                   Linux/Mac: g++ -std=c++11 -pthread -o controle
 *                     controle_entregas_retiradas_estoque.cpp
 *                   Visual Studio: projeto de console, nada a configurar.
 *  Versao ........: 3.0
 * ==========================================================================
 */
#ifdef _WIN32
  #define _WINSOCK_DEPRECATED_NO_WARNINGS
  #define _CRT_SECURE_NO_WARNINGS
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
  #ifdef _MSC_VER
    #pragma comment(lib, "ws2_32.lib")
  #endif
  typedef SOCKET Sock;
  #define FECHAR closesocket
  #define FLAG_ENVIO 0
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <pthread.h>
  typedef int Sock;
  #define INVALID_SOCKET (-1)
  #define FECHAR close
  #define FLAG_ENVIO MSG_NOSIGNAL
#endif
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <ctime>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace std;

/* ------------------------------ ARQUIVOS ------------------------------ */
const string ARQ_CLIENTES = "clientes.csv";
const string ARQ_SERVICOS = "servicos.csv";
const string ARQ_ITENS    = "itens.csv";
const string ARQ_CONFIG   = "config.csv";   // versao 1 (so lido para migrar)

/* ------------------------------ REGISTROS ----------------------------- */
struct Cliente {
    int id;
    string nome;
    string telefone;
    string endereco;
    string bairro;
};

struct Item {               // um item de locacao: mesa, cadeira, toalha...
    int id;
    string nome;
    int total;              // quantidade que a empresa possui
    int minimo;             // abaixo disso o sistema avisa
    bool ativo;
};

struct Qtd {                // quantidade de um item dentro de um servico
    int itemId;
    int qtd;
};

struct Servico {            // uma entrega OU uma retirada
    int id;
    int clienteId;
    char tipo;              // 'E' = entrega, 'R' = retirada
    string data;            // DD/MM/AAAA
    string horario;         // HH:MM (obrigatorio na retirada)
    string rota;            // A1, A2, B1... (letra = viagem, numero = parada)
    vector<Qtd> itens;
    string outros;          // servico extra / o que sera feito
    string endereco;        // o mesmo na entrega e na retirada
    string bairro;          // "DEPOSITO" quando o cliente busca/devolve
    string status;          // PENDENTE, FEITO, CANCELADO
    string motorista;       // quem deu a baixa
    string obs;
    int vinculo;            // id da entrega/retirada par (0 = sem par)
};

vector<Cliente> clientes;
vector<Item> itens;
vector<Servico> servicos;

/* Trava dos dados: o servidor dos celulares roda em paralelo com o menu,
   entao toda alteracao nos vetores acontece com a trava fechada. */
#ifdef _WIN32
CRITICAL_SECTION travaDados;
void iniciarTrava() { InitializeCriticalSection(&travaDados); }
struct Trava { Trava() { EnterCriticalSection(&travaDados); } ~Trava() { LeaveCriticalSection(&travaDados); } };
#else
pthread_mutex_t travaDados;
void iniciarTrava() {
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&travaDados, &a);
}
struct Trava { Trava() { pthread_mutex_lock(&travaDados); } ~Trava() { pthread_mutex_unlock(&travaDados); } };
#endif

void gerarPainelArquivo();   // definida no modulo do painel, mais abaixo

/* ======================================================================
 *                       FUNCOES DE TEXTO E ENTRADA
 * ====================================================================== */
string trim(const string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

string maiusculas(string s) {
    for (size_t i = 0; i < s.size(); i++) s[i] = (char)toupper((unsigned char)s[i]);
    return s;
}

bool contem(const string& texto, const string& busca) {
    return maiusculas(texto).find(maiusculas(busca)) != string::npos;
}

/* Ajusta um texto para ocupar exatamente 'w' colunas + 1 espaco. */
string col(const string& s, size_t w) {
    string r = s.size() > w ? s.substr(0, w - 1) + "." : s;
    return r + string(w - r.size() + 1, ' ');
}

/* Alinha um numero a direita em 'w' colunas + 1 espaco. */
string colNum(int n, size_t w) {
    string s = to_string(n);
    if (s.size() < w) s = string(w - s.size(), ' ') + s;
    return s + " ";
}

vector<string> dividir(const string& linha, char sep) {
    vector<string> partes;
    string campo;
    istringstream ss(linha);
    while (getline(ss, campo, sep)) partes.push_back(campo);
    if (!linha.empty() && linha[linha.size() - 1] == sep) partes.push_back("");
    return partes;
}

bool ehNumero(const string& s) {
    if (s.empty() || s.size() > 9) return false;
    for (size_t i = 0; i < s.size(); i++)
        if (!isdigit((unsigned char)s[i])) return false;
    return true;
}

/* Le uma linha. ';' e '|' viram ',' para nao quebrar o CSV. */
string lerLinha(const string& prompt) {
    cout << prompt;
    string s;
    if (!getline(cin, s)) {
        cout << "\n[Entrada encerrada. Os dados ja estao salvos.]\n";
        exit(0);
    }
    s = trim(s);
    for (size_t i = 0; i < s.size(); i++) if (s[i] == ';' || s[i] == '|') s[i] = ',';
    return s;
}

string lerTexto(const string& prompt, bool obrigatorio) {
    while (true) {
        string s = lerLinha(prompt);
        if (!s.empty() || !obrigatorio) return s;
        cout << "  ! Campo obrigatorio.\n";
    }
}

int lerInteiro(const string& prompt, int minimo, int maximo) {
    while (true) {
        string s = lerLinha(prompt);
        if (ehNumero(s)) {
            int v = atoi(s.c_str());
            if (v >= minimo && v <= maximo) return v;
        }
        cout << "  ! Digite um numero entre " << minimo << " e " << maximo << ".\n";
    }
}

/* Igual a lerInteiro, mas Enter vazio devolve o valor padrao. */
int lerInteiroPadrao(const string& prompt, int minimo, int maximo, int padrao) {
    while (true) {
        string s = lerLinha(prompt);
        if (s.empty()) return padrao;
        if (ehNumero(s)) {
            int v = atoi(s.c_str());
            if (v >= minimo && v <= maximo) return v;
        }
        cout << "  ! Digite um numero entre " << minimo << " e " << maximo << ".\n";
    }
}

bool lerSimNao(const string& prompt) {
    while (true) {
        string s = maiusculas(lerLinha(prompt + " (S/N): "));
        if (s == "S" || s == "SIM") return true;
        if (s == "N" || s == "NAO") return false;
        cout << "  ! Responda S ou N.\n";
    }
}

/* ======================================================================
 *                          DATAS, HORARIOS E ROTAS
 * ====================================================================== */
bool bissexto(int a) { return (a % 4 == 0 && a % 100 != 0) || a % 400 == 0; }

bool dataValida(const string& s) {
    if (s.size() != 10 || s[2] != '/' || s[5] != '/') return false;
    int pos[] = {0, 1, 3, 4, 6, 7, 8, 9};
    for (int i = 0; i < 8; i++) if (!isdigit((unsigned char)s[pos[i]])) return false;
    int d = atoi(s.substr(0, 2).c_str());
    int m = atoi(s.substr(3, 2).c_str());
    int a = atoi(s.substr(6, 4).c_str());
    if (a < 2000 || a > 2100 || m < 1 || m > 12 || d < 1) return false;
    int dias[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m == 2 && bissexto(a)) dias[1] = 29;
    return d <= dias[m - 1];
}

/* DD/MM/AAAA -> AAAAMMDD (numero que permite comparar e ordenar datas). */
int chaveData(const string& s) {
    return atoi(s.substr(6, 4).c_str()) * 10000 +
           atoi(s.substr(3, 2).c_str()) * 100 +
           atoi(s.substr(0, 2).c_str());
}

string hoje() {
    time_t t = time(NULL);
    tm* l = localtime(&t);
    char b[16];
    strftime(b, sizeof b, "%d/%m/%Y", l);
    return b;
}

tm montarTm(const string& s, int somaDias) {
    tm x = tm();
    x.tm_mday = atoi(s.substr(0, 2).c_str()) + somaDias;
    x.tm_mon = atoi(s.substr(3, 2).c_str()) - 1;
    x.tm_year = atoi(s.substr(6, 4).c_str()) - 1900;
    x.tm_hour = 12;
    mktime(&x);
    return x;
}

string somarDias(const string& s, int n) {
    tm x = montarTm(s, n);
    char b[16];
    strftime(b, sizeof b, "%d/%m/%Y", &x);
    return b;
}

string diaSemana(const string& s) {
    const char* nomes[] = {"DOMINGO", "SEGUNDA", "TERCA", "QUARTA",
                           "QUINTA", "SEXTA", "SABADO"};
    tm x = montarTm(s, 0);
    return nomes[x.tm_wday];
}

/* Aceita D/M, DD/MM, DD/MM/AA e DD/MM/AAAA. Devolve "" se invalida. */
string normalizarData(const string& s) {
    vector<string> p = dividir(s, '/');
    if (p.size() < 2 || p.size() > 3) return "";
    for (size_t i = 0; i < p.size(); i++) if (!ehNumero(p[i])) return "";
    int d = atoi(p[0].c_str()), m = atoi(p[1].c_str()), a;
    if (p.size() == 2) a = atoi(hoje().substr(6, 4).c_str());
    else {
        a = atoi(p[2].c_str());
        if (p[2].size() == 2) a += 2000;
    }
    char b[48];
    snprintf(b, sizeof b, "%02d/%02d/%04d", d, m, a);
    string r = b;
    return dataValida(r) ? r : "";
}

string lerData(const string& prompt, bool enterHoje) {
    while (true) {
        string s = lerLinha(prompt + (enterHoje ? " (DD/MM/AAAA, Enter = hoje): "
                                                : " (DD/MM/AAAA): "));
        if (s.empty() && enterHoje) return hoje();
        string d = normalizarData(s);
        if (!d.empty()) return d;
        cout << "  ! Data invalida.\n";
    }
}

/* Aceita 8, 8h, 8h30, 8:30, 08:30. "" = sem horario. "?" = invalido. */
string normalizarHorario(const string& s) {
    if (s.empty()) return "";
    string t = maiusculas(s);
    for (size_t i = 0; i < t.size(); i++) if (t[i] == 'H') t[i] = ':';
    if (!t.empty() && t[t.size() - 1] == ':') t.erase(t.size() - 1);
    size_t p = t.find(':');
    string hs = (p == string::npos) ? t : t.substr(0, p);
    string ms = (p == string::npos) ? "0" : t.substr(p + 1);
    if (!ehNumero(hs) || !ehNumero(ms) || hs.size() > 2 || ms.size() > 2) return "?";
    int h = atoi(hs.c_str()), m = atoi(ms.c_str());
    if (h > 23 || m > 59) return "?";
    char b[32];
    snprintf(b, sizeof b, "%02d:%02d", h, m);
    return b;
}

string lerHorario(const string& prompt, bool obrigatorio) {
    while (true) {
        string h = normalizarHorario(lerLinha(prompt));
        if (h == "?") { cout << "  ! Horario invalido. Exemplos: 8, 8h30, 14:00.\n"; continue; }
        if (h.empty() && obrigatorio) { cout << "  ! O horario e obrigatorio.\n"; continue; }
        return h;
    }
}

string normalizarRota(const string& s) {
    string r = maiusculas(s);
    r.erase(remove(r.begin(), r.end(), ' '), r.end());
    return r;
}

/* Rota valida: vazia, ou uma letra seguida de 1 ou 2 digitos (A1, B12). */
bool rotaValida(const string& r) {
    if (r.empty()) return true;
    if (r.size() < 2 || r.size() > 3) return false;
    if (r[0] < 'A' || r[0] > 'Z') return false;
    for (size_t i = 1; i < r.size(); i++)
        if (!isdigit((unsigned char)r[i])) return false;
    return true;
}

/* A1 < A2 < A10 < B1 ... ; sem rota vai para o fim. */
int chaveRota(const string& r) {
    if (r.empty()) return 99999;
    return (r[0] - 'A') * 100 + atoi(r.substr(1).c_str());
}

/* ======================================================================
 *                              BUSCAS
 * ====================================================================== */
int indiceCliente(int id) {
    for (size_t i = 0; i < clientes.size(); i++) if (clientes[i].id == id) return (int)i;
    return -1;
}

int indiceItem(int id) {
    for (size_t i = 0; i < itens.size(); i++) if (itens[i].id == id) return (int)i;
    return -1;
}

int indiceServico(int id) {
    for (size_t i = 0; i < servicos.size(); i++) if (servicos[i].id == id) return (int)i;
    return -1;
}

string nomeCliente(int id) {
    int i = indiceCliente(id);
    return i < 0 ? "(cliente nao encontrado)" : clientes[i].nome;
}

string nomeItem(int id) {
    int i = indiceItem(id);
    return i < 0 ? "item " + to_string(id) : itens[i].nome;
}

int proximoIdCliente() {
    int m = 0;
    for (size_t i = 0; i < clientes.size(); i++) m = max(m, clientes[i].id);
    return m + 1;
}

int proximoIdItem() {
    int m = 0;
    for (size_t i = 0; i < itens.size(); i++) m = max(m, itens[i].id);
    return m + 1;
}

int proximoIdServico() {
    int m = 0;
    for (size_t i = 0; i < servicos.size(); i++) m = max(m, servicos[i].id);
    return m + 1;
}

int qtdDoItem(const Servico& s, int itemId) {
    for (size_t i = 0; i < s.itens.size(); i++)
        if (s.itens[i].itemId == itemId) return s.itens[i].qtd;
    return 0;
}

/* "5 Mesa, 40 Cadeira" */
string textoItens(const Servico& s) {
    string r;
    for (size_t i = 0; i < s.itens.size(); i++) {
        if (!r.empty()) r += ", ";
        r += to_string(s.itens[i].qtd) + " " + nomeItem(s.itens[i].itemId);
    }
    if (!s.outros.empty()) r += (r.empty() ? "" : " + ") + s.outros;
    return r.empty() ? "-" : r;
}

/* Ordem da programacao: rota (A1, A2, B1...), depois horario, depois id. */
bool vemAntes(const Servico& a, const Servico& b) {
    if (chaveRota(a.rota) != chaveRota(b.rota)) return chaveRota(a.rota) < chaveRota(b.rota);
    string ha = a.horario.empty() ? "99:99" : a.horario;
    string hb = b.horario.empty() ? "99:99" : b.horario;
    if (ha != hb) return ha < hb;
    return a.id < b.id;
}

bool vemAntesComData(const Servico& a, const Servico& b) {
    if (chaveData(a.data) != chaveData(b.data)) return chaveData(a.data) < chaveData(b.data);
    return vemAntes(a, b);
}

/* ======================================================================
 *                         PERSISTENCIA EM ARQUIVO
 * ====================================================================== */

/* Itens de um servico no CSV: "1:5|2:40" (id do item : quantidade). */
string itensParaCsv(const vector<Qtd>& v) {
    string r;
    for (size_t i = 0; i < v.size(); i++) {
        if (i) r += "|";
        r += to_string(v[i].itemId) + ":" + to_string(v[i].qtd);
    }
    return r;
}

bool csvParaItens(const string& s, vector<Qtd>& v) {
    v.clear();
    if (s.empty()) return true;
    vector<string> partes = dividir(s, '|');
    for (size_t i = 0; i < partes.size(); i++) {
        vector<string> p = dividir(partes[i], ':');
        if (p.size() != 2 || !ehNumero(p[0]) || !ehNumero(p[1])) return false;
        Qtd q; q.itemId = atoi(p[0].c_str()); q.qtd = atoi(p[1].c_str());
        if (q.qtd > 0) v.push_back(q);
    }
    return true;
}

void salvarClientes() {
    Trava t;
    ofstream f(ARQ_CLIENTES.c_str());
    f << "id;nome;telefone;endereco;bairro\n";
    for (size_t i = 0; i < clientes.size(); i++) {
        const Cliente& c = clientes[i];
        f << c.id << ';' << c.nome << ';' << c.telefone << ';'
          << c.endereco << ';' << c.bairro << '\n';
    }
}

void salvarItens() {
    Trava t;
    ofstream f(ARQ_ITENS.c_str());
    f << "id;nome;total;minimo;ativo\n";
    for (size_t i = 0; i < itens.size(); i++) {
        const Item& it = itens[i];
        f << it.id << ';' << it.nome << ';' << it.total << ';' << it.minimo << ';'
          << (it.ativo ? 1 : 0) << '\n';
    }
    f.close();
    gerarPainelArquivo();
}

void salvarServicos() {
    Trava t;
    ofstream f(ARQ_SERVICOS.c_str());
    f << "id;cliente_id;tipo;data;horario;rota;itens;outros;"
         "endereco;bairro;status;motorista;obs;vinculo\n";
    for (size_t i = 0; i < servicos.size(); i++) {
        const Servico& s = servicos[i];
        f << s.id << ';' << s.clienteId << ';' << s.tipo << ';' << s.data << ';'
          << s.horario << ';' << s.rota << ';' << itensParaCsv(s.itens) << ';'
          << s.outros << ';' << s.endereco << ';' << s.bairro << ';' << s.status << ';'
          << s.motorista << ';' << s.obs << ';' << s.vinculo << '\n';
    }
    f.close();
    gerarPainelArquivo();
}

void carregarClientes() {
    ifstream f(ARQ_CLIENTES.c_str());
    string linha;
    getline(f, linha);                            // cabecalho
    while (getline(f, linha)) {
        if (trim(linha).empty()) continue;
        vector<string> p = dividir(trim(linha), ';');
        if (p.size() != 5 || !ehNumero(p[0])) {
            cout << "  ! Linha ignorada em " << ARQ_CLIENTES << ": " << linha << '\n';
            continue;
        }
        Cliente c;
        c.id = atoi(p[0].c_str());
        c.nome = p[1]; c.telefone = p[2]; c.endereco = p[3]; c.bairro = p[4];
        clientes.push_back(c);
    }
}

/* Se itens.csv nao existe (primeira vez ou dados da versao 1), cria o
   catalogo com Mesa (id 1) e Cadeira (id 2), usando o estoque antigo
   gravado em config.csv quando houver. */
void criarCatalogoInicial() {
    int mesas = 0, cadeiras = 0;
    ifstream f(ARQ_CONFIG.c_str());
    string linha;
    getline(f, linha);
    while (getline(f, linha)) {
        vector<string> p = dividir(trim(linha), ';');
        if (p.size() != 2 || !ehNumero(p[1])) continue;
        if (p[0] == "ESTOQUE_MESAS") mesas = atoi(p[1].c_str());
        if (p[0] == "ESTOQUE_CADEIRAS") cadeiras = atoi(p[1].c_str());
    }
    Item m;  m.id = 1; m.nome = "Mesa";    m.total = mesas;    m.minimo = 0; m.ativo = true;
    Item c;  c.id = 2; c.nome = "Cadeira"; c.total = cadeiras; c.minimo = 0; c.ativo = true;
    itens.push_back(m);
    itens.push_back(c);
    salvarItens();
}

void carregarItens() {
    ifstream f(ARQ_ITENS.c_str());
    if (!f) { criarCatalogoInicial(); return; }
    string linha;
    getline(f, linha);
    while (getline(f, linha)) {
        if (trim(linha).empty()) continue;
        vector<string> p = dividir(trim(linha), ';');
        if (p.size() != 5 || !ehNumero(p[0]) || !ehNumero(p[2]) || !ehNumero(p[3])) {
            cout << "  ! Linha ignorada em " << ARQ_ITENS << ": " << linha << '\n';
            continue;
        }
        Item it;
        it.id = atoi(p[0].c_str()); it.nome = p[1];
        it.total = atoi(p[2].c_str()); it.minimo = atoi(p[3].c_str());
        it.ativo = p[4] != "0";
        itens.push_back(it);
    }
    if (itens.empty()) criarCatalogoInicial();
}

/* Le o formato atual (14 colunas) e tambem o da versao 1 (15 colunas,
   com mesas e cadeiras em colunas separadas). */
void carregarServicos() {
    ifstream f(ARQ_SERVICOS.c_str());
    string linha;
    getline(f, linha);
    bool migrou = false;
    while (getline(f, linha)) {
        if (trim(linha).empty()) continue;
        vector<string> p = dividir(trim(linha), ';');
        Servico s;
        bool ok = false;
        if (p.size() == 14 && ehNumero(p[0]) && ehNumero(p[1]) && p[2].size() == 1 &&
            dataValida(p[3]) && ehNumero(p[13]) && csvParaItens(p[6], s.itens)) {
            s.outros = p[7]; s.endereco = p[8]; s.bairro = p[9]; s.status = p[10];
            s.motorista = p[11]; s.obs = p[12]; s.vinculo = atoi(p[13].c_str());
            ok = true;
        } else if (p.size() == 15 && ehNumero(p[0]) && ehNumero(p[1]) && p[2].size() == 1 &&
                   dataValida(p[3]) && ehNumero(p[6]) && ehNumero(p[7]) && ehNumero(p[14])) {
            Qtd m; m.itemId = 1; m.qtd = atoi(p[6].c_str());
            Qtd c; c.itemId = 2; c.qtd = atoi(p[7].c_str());
            if (m.qtd > 0) s.itens.push_back(m);
            if (c.qtd > 0) s.itens.push_back(c);
            s.outros = p[8]; s.endereco = p[9]; s.bairro = p[10]; s.status = p[11];
            s.motorista = p[12]; s.obs = p[13]; s.vinculo = atoi(p[14].c_str());
            ok = migrou = true;
        }
        if (!ok) {
            cout << "  ! Linha ignorada em " << ARQ_SERVICOS << ": " << linha << '\n';
            continue;
        }
        s.id = atoi(p[0].c_str()); s.clienteId = atoi(p[1].c_str());
        s.tipo = p[2][0]; s.data = p[3]; s.horario = p[4]; s.rota = p[5];
        servicos.push_back(s);
    }
    if (migrou) {
        salvarServicos();
        cout << "  Arquivo " << ARQ_SERVICOS << " convertido para o formato com itens.\n";
    }
}

/* ======================================================================
 *                    ESTOQUE E DISPONIBILIDADE (RF16-RF19)
 * ----------------------------------------------------------------------
 *  Um item fica comprometido do dia da entrega ate o dia da retirada
 *  (os dois inclusive). Entrega sem retirada fica comprometida sem fim.
 * ====================================================================== */
int fimDoPeriodo(const Servico& entrega) {
    int iv = entrega.vinculo ? indiceServico(entrega.vinculo) : -1;
    if (iv >= 0 && servicos[iv].status != "CANCELADO") return chaveData(servicos[iv].data);
    return 99999999;
}

/* Quantidade de um item comprometida em um dia. 'ignorar' deixa de fora
   um pedido (usado ao reagendar o proprio pedido). */
int comprometido(int itemId, const string& dia, int ignorar) {
    int k = chaveData(dia), soma = 0;
    for (size_t i = 0; i < servicos.size(); i++) {
        const Servico& e = servicos[i];
        if (e.tipo != 'E' || e.status == "CANCELADO" || e.id == ignorar) continue;
        if (chaveData(e.data) <= k && k <= fimDoPeriodo(e)) soma += qtdDoItem(e, itemId);
    }
    return soma;
}

/* Maior quantidade comprometida entre dois dias (pico do periodo). */
int picoNoPeriodo(int itemId, const string& ini, const string& fim, int ignorar, string* diaPico) {
    int pico = 0;
    string d = ini;
    for (int n = 0; n < 400 && chaveData(d) <= chaveData(fim); n++, d = somarDias(d, 1)) {
        int c = comprometido(itemId, d, ignorar);
        if (c > pico || n == 0) { pico = c; if (diaPico) *diaPico = d; }
    }
    return pico;
}

int disponivelNoPeriodo(int itemId, const string& ini, const string& fim, int ignorar) {
    int i = indiceItem(itemId);
    int total = i < 0 ? 0 : itens[i].total;
    return total - picoNoPeriodo(itemId, ini, fim, ignorar, NULL);
}

/* Itens que sairam (entrega com baixa) e ainda nao voltaram. */
int naRua(int itemId) {
    int soma = 0;
    for (size_t i = 0; i < servicos.size(); i++) {
        const Servico& e = servicos[i];
        if (e.tipo != 'E' || e.status != "FEITO") continue;
        int iv = e.vinculo ? indiceServico(e.vinculo) : -1;
        if (iv >= 0 && servicos[iv].status == "FEITO") continue;
        soma += qtdDoItem(e, itemId);
    }
    return soma;
}

/* Confere um pedido contra o estoque e imprime os avisos.
   Devolve false se faltar item em algum dia do periodo. */
bool conferirEstoque(const vector<Qtd>& pedido, const string& ini, const string& fim, int ignorar) {
    bool falta = false;
    for (size_t i = 0; i < pedido.size(); i++) {
        int ii = indiceItem(pedido[i].itemId);
        if (ii < 0) continue;
        string dia;
        int pico = picoNoPeriodo(pedido[i].itemId, ini, fim, ignorar, &dia);
        int sobra = itens[ii].total - pico - pedido[i].qtd;
        if (sobra < 0) {
            cout << "  !! FALTA " << itens[ii].nome << ": pedido " << pedido[i].qtd
                 << ", disponivel " << itens[ii].total - pico << " em " << dia
                 << " (faltam " << -sobra << ").\n";
            falta = true;
        } else if (sobra < itens[ii].minimo) {
            cout << "  !  ESTOQUE BAIXO de " << itens[ii].nome << ": sobram " << sobra
                 << " em " << dia << " (minimo " << itens[ii].minimo << ").\n";
        }
    }
    return !falta;
}

/* Menor saldo de cada item nos proximos 7 dias; conta itens em alerta. */
int contarAlertas() {
    int n = 0;
    for (size_t i = 0; i < itens.size(); i++) {
        if (!itens[i].ativo) continue;
        int saldo = itens[i].total - picoNoPeriodo(itens[i].id, hoje(), somarDias(hoje(), 6), 0, NULL);
        if (saldo < itens[i].minimo || saldo < 0) n++;
    }
    return n;
}

/* ======================================================================
 *                              EXIBICAO
 * ====================================================================== */
void imprimirCabecalhoClientes() {
    cout << col("ID", 4) << col("NOME", 24) << col("TELEFONE", 15) << "BAIRRO\n";
    cout << string(64, '-') << '\n';
}

void imprimirCliente(const Cliente& c) {
    cout << col(to_string(c.id), 4) << col(c.nome, 24) << col(c.telefone, 15) << c.bairro << '\n';
}

string cabecalhoServicos(bool comData) {
    string s = "    ";
    if (comData) s += col("DATA", 10);
    s += col("ID", 4) + col("ROTA", 4) + col("TIPO", 8) + col("HORA", 5) +
         col("CLIENTE", 20) + col("BAIRRO", 14) + "ITENS";
    return s + "\n" + string(comData ? 100 : 89, '-') + "\n";
}

string linhaServico(const Servico& s, bool comData, bool detalhes) {
    ostringstream o;
    string marca = s.status == "FEITO" ? "[X]" : (s.status == "CANCELADO" ? "[C]" : "[ ]");
    o << marca << ' ';
    if (comData) o << col(s.data, 10);
    o << col(to_string(s.id), 4) << col(s.rota.empty() ? "-" : s.rota, 4)
      << col(s.tipo == 'E' ? "ENTREGA" : "RETIRADA", 8)
      << col(s.horario.empty() ? "--:--" : s.horario, 5)
      << col(nomeCliente(s.clienteId), 20) << col(s.bairro, 14)
      << textoItens(s) << '\n';
    if (detalhes) {
        int ic = indiceCliente(s.clienteId);
        string extra;
        if (!s.endereco.empty()) extra += "End.: " + s.endereco;
        if (ic >= 0 && !clientes[ic].telefone.empty())
            extra += (extra.empty() ? "" : " | ") + string("Tel: ") + clientes[ic].telefone;
        if (!s.obs.empty()) extra += (extra.empty() ? "" : " | ") + string("Obs: ") + s.obs;
        int iv = s.vinculo ? indiceServico(s.vinculo) : -1;
        if (iv >= 0)
            extra += (extra.empty() ? "" : " | ") + string(s.tipo == 'E' ? "Retirada: " : "Entregue: ")
                     + servicos[iv].data;
        if (s.status == "FEITO" && !s.motorista.empty())
            extra += (extra.empty() ? "" : " | ") + string("Baixa: ") + s.motorista;
        if (!extra.empty()) o << "        " << extra << '\n';
    }
    return o.str();
}

/* ======================================================================
 *                            CLIENTES (RF01-RF03)
 * ====================================================================== */
int cadastrarCliente() {
    cout << "\n=== NOVO CLIENTE ===\n";
    string nome = lerTexto("Nome: ", true);
    for (size_t i = 0; i < clientes.size(); i++) {
        if (maiusculas(clientes[i].nome) == maiusculas(nome)) {
            cout << "  ! Ja existe um cliente com esse nome (ID " << clientes[i].id << ").\n";
            if (!lerSimNao("Cadastrar mesmo assim?")) return clientes[i].id;
            break;
        }
    }
    Cliente c;
    c.id = proximoIdCliente();
    c.nome = nome;
    c.telefone = lerTexto("Telefone (opcional): ", false);
    c.endereco = lerTexto("Endereco (opcional): ", false);
    c.bairro = maiusculas(lerTexto("Bairro: ", true));
    {
        Trava t;
        clientes.push_back(c);
        salvarClientes();
    }
    cout << "Cliente cadastrado com ID " << c.id << ".\n";
    return c.id;
}

void pesquisarClientes() {
    string termo = lerTexto("\nDigite parte do nome ou do telefone: ", true);
    int achados = 0;
    for (size_t i = 0; i < clientes.size(); i++) {
        if (contem(clientes[i].nome, termo) || contem(clientes[i].telefone, termo)) {
            if (achados == 0) imprimirCabecalhoClientes();
            imprimirCliente(clientes[i]);
            achados++;
        }
    }
    if (achados == 0) cout << "Nenhum cliente encontrado.\n";
    else cout << achados << " cliente(s) encontrado(s).\n";
}

void listarClientes() {
    if (clientes.empty()) { cout << "\nNenhum cliente cadastrado.\n"; return; }
    vector<Cliente> copia = clientes;
    for (size_t i = 0; i < copia.size(); i++)             // ordenacao por insercao
        for (size_t j = i; j > 0 && maiusculas(copia[j].nome) < maiusculas(copia[j - 1].nome); j--)
            swap(copia[j], copia[j - 1]);
    cout << '\n';
    imprimirCabecalhoClientes();
    for (size_t i = 0; i < copia.size(); i++) imprimirCliente(copia[i]);
    cout << copia.size() << " cliente(s).\n";
}

void editarCliente() {
    int id = lerInteiro("\nID do cliente (0 = voltar): ", 0, 999999999);
    if (id == 0) return;
    int i = indiceCliente(id);
    if (i < 0) { cout << "Cliente nao encontrado.\n"; return; }
    Cliente c = clientes[i];                         // edita uma copia
    cout << "Enter mantem o valor atual.\n";
    string v;
    v = lerLinha("Nome [" + c.nome + "]: ");          if (!v.empty()) c.nome = v;
    v = lerLinha("Telefone [" + c.telefone + "]: ");  if (!v.empty()) c.telefone = v;
    v = lerLinha("Endereco [" + c.endereco + "]: ");  if (!v.empty()) c.endereco = v;
    v = lerLinha("Bairro [" + c.bairro + "]: ");      if (!v.empty()) c.bairro = maiusculas(v);
    Trava t;
    clientes[i] = c;
    salvarClientes();
    cout << "Cliente atualizado.\n";
}

/* Escolhe um cliente por ID ou parte do nome. 'N' cadastra um novo. */
int selecionarCliente() {
    while (true) {
        string s = lerLinha("\nCliente: digite o ID ou parte do nome (N = novo, 0 = cancelar): ");
        if (s == "0") return 0;
        if (maiusculas(s) == "N") return cadastrarCliente();
        if (s.empty()) continue;
        if (ehNumero(s)) {
            int i = indiceCliente(atoi(s.c_str()));
            if (i >= 0) { cout << "-> " << clientes[i].nome << '\n'; return clientes[i].id; }
            cout << "  ! ID nao encontrado.\n";
            continue;
        }
        vector<int> achados;
        for (size_t i = 0; i < clientes.size(); i++)
            if (contem(clientes[i].nome, s)) achados.push_back((int)i);
        if (achados.empty()) {
            cout << "  ! Nenhum cliente com esse nome.\n";
            if (lerSimNao("Cadastrar novo cliente?")) return cadastrarCliente();
            continue;
        }
        if (achados.size() == 1) {
            cout << "-> " << clientes[achados[0]].nome << '\n';
            return clientes[achados[0]].id;
        }
        imprimirCabecalhoClientes();
        for (size_t k = 0; k < achados.size(); k++) imprimirCliente(clientes[achados[k]]);
        cout << "Mais de um cliente encontrado; digite o ID.\n";
    }
}

void menuClientes() {
    while (true) {
        cout << "\n----- CLIENTES -----\n"
                "1 - Cadastrar cliente\n"
                "2 - Pesquisar cliente\n"
                "3 - Listar todos\n"
                "4 - Editar cliente\n"
                "0 - Voltar\n";
        int op = lerInteiro("Opcao: ", 0, 4);
        if (op == 0) return;
        if (op == 1) cadastrarCliente();
        if (op == 2) pesquisarClientes();
        if (op == 3) listarClientes();
        if (op == 4) editarCliente();
    }
}

/* ======================================================================
 *                      ITENS E ESTOQUE (RF16-RF19)
 * ====================================================================== */
void cadastrarItem() {
    cout << "\n=== NOVO ITEM DE LOCACAO ===\n";
    string nome = lerTexto("Nome (ex.: Toalha branca, Tenda 3x3): ", true);
    for (size_t i = 0; i < itens.size(); i++)
        if (maiusculas(itens[i].nome) == maiusculas(nome)) {
            cout << "  ! Ja existe o item " << itens[i].nome << " (ID " << itens[i].id << ").\n";
            return;
        }
    Item it;
    it.id = proximoIdItem();
    it.nome = nome;
    it.total = lerInteiro("Quantidade total que a empresa possui: ", 0, 999999);
    it.minimo = lerInteiroPadrao("Estoque minimo para avisar (Enter = 0): ", 0, 999999, 0);
    it.ativo = true;
    {
        Trava t;
        itens.push_back(it);
        salvarItens();
    }
    cout << "Item cadastrado com ID " << it.id << ".\n";
}

void editarItem() {
    int id = lerInteiro("\nID do item (0 = voltar): ", 0, 999999999);
    if (id == 0) return;
    int i = indiceItem(id);
    if (i < 0) { cout << "Item nao encontrado.\n"; return; }
    Item it = itens[i];                              // edita uma copia
    cout << "Enter mantem o valor atual.\n";
    string v = lerLinha("Nome [" + it.nome + "]: ");
    if (!v.empty()) it.nome = v;
    it.total = lerInteiroPadrao("Quantidade total [" + to_string(it.total) + "]: ", 0, 999999, it.total);
    it.minimo = lerInteiroPadrao("Estoque minimo [" + to_string(it.minimo) + "]: ", 0, 999999, it.minimo);
    if (it.ativo) { if (lerSimNao("Desativar o item (deixa de aparecer nos pedidos)?")) it.ativo = false; }
    else if (lerSimNao("Item desativado. Reativar?")) it.ativo = true;
    Trava t;
    itens[i] = it;
    salvarItens();
    cout << "Item atualizado.\n";
}

void relatorioEstoque() {
    string ini = hoje(), fim = somarDias(hoje(), 6);
    cout << "\n=== ESTOQUE DOS ITENS (hoje e proximos 7 dias) ===\n"
         << col("ID", 3) << col("ITEM", 18) << col("TOTAL", 6) << col("MINIMO", 6)
         << col("NA RUA", 6) << col("LIVRE HOJE", 10) << col("MENOR SALDO 7D", 16) << "SITUACAO\n"
         << string(90, '-') << '\n';
    for (size_t i = 0; i < itens.size(); i++) {
        const Item& it = itens[i];
        if (!it.ativo) continue;
        string dia;
        int livreHoje = it.total - comprometido(it.id, ini, 0);
        int pico = picoNoPeriodo(it.id, ini, fim, 0, &dia);
        int menor = it.total - pico;
        string sit = menor < 0 ? "FALTA" : (menor < it.minimo ? "BAIXO" : "OK");
        cout << col(to_string(it.id), 3) << col(it.nome, 18) << colNum(it.total, 6)
             << colNum(it.minimo, 6) << colNum(naRua(it.id), 6) << colNum(livreHoje, 10)
             << col(to_string(menor) + " em " + dia.substr(0, 5), 16) << sit << '\n';
    }
    bool algumInativo = false;
    for (size_t i = 0; i < itens.size(); i++) if (!itens[i].ativo) algumInativo = true;
    if (algumInativo) {
        cout << "Desativados:";
        for (size_t i = 0; i < itens.size(); i++)
            if (!itens[i].ativo) cout << ' ' << itens[i].nome << " (ID " << itens[i].id << ")";
        cout << '\n';
    }
    cout << "NA RUA = saiu com baixa e ainda nao voltou. LIVRE = total menos o que esta\n"
            "reservado em pedidos (da entrega ate a retirada).\n";
}

void disponibilidadeEmPeriodo() {
    string ini = lerData("\nDe", true);
    string fim;
    while (true) {
        fim = lerData("Ate", false);
        if (chaveData(fim) >= chaveData(ini)) break;
        cout << "  ! A data final nao pode ser antes da inicial.\n";
    }
    cout << "\nDisponivel em todos os dias de " << ini << " a " << fim << ":\n";
    for (size_t i = 0; i < itens.size(); i++) {
        if (!itens[i].ativo) continue;
        int d = disponivelNoPeriodo(itens[i].id, ini, fim, 0);
        cout << "  " << col(itens[i].nome, 18) << colNum(d, 6) << "de " << itens[i].total
             << (d < itens[i].minimo ? "   <- abaixo do minimo" : "") << '\n';
    }
}

void menuItens() {
    while (true) {
        cout << "\n----- ITENS E ESTOQUE -----\n"
                "1 - Ver estoque e alertas\n"
                "2 - Cadastrar item\n"
                "3 - Editar item (nome, total, minimo, desativar)\n"
                "4 - Disponibilidade em um periodo\n"
                "0 - Voltar\n";
        int op = lerInteiro("Opcao: ", 0, 4);
        if (op == 0) return;
        if (op == 1) relatorioEstoque();
        if (op == 2) cadastrarItem();
        if (op == 3) editarItem();
        if (op == 4) disponibilidadeEmPeriodo();
    }
}

/* ======================================================================
 *                 NOVO PEDIDO: ENTREGA + RETIRADA (RF04-RF06)
 * ====================================================================== */

/* Avisa se a rota ja esta ocupada na mesma data. Devolve false se o
   usuario desistir. */
bool conferirRotaLivre(const string& data, const string& rota, int ignorarId) {
    if (rota.empty()) return true;
    for (size_t i = 0; i < servicos.size(); i++) {
        const Servico& s = servicos[i];
        if (s.id != ignorarId && s.status != "CANCELADO" && s.data == data && s.rota == rota) {
            cout << "  ! A rota " << rota << " ja esta com " << nomeCliente(s.clienteId)
                 << " (ID " << s.id << ") nesse dia.\n";
            return lerSimNao("Usar a mesma rota assim mesmo?");
        }
    }
    return true;
}

string lerRota(const string& prompt, const string& data, int ignorarId, const string& atual) {
    while (true) {
        string r = normalizarRota(lerLinha(prompt));
        if (r.empty()) r = atual;
        if (!rotaValida(r)) { cout << "  ! Use uma letra e um numero, ex.: A1, B2.\n"; continue; }
        if (conferirRotaLivre(data, r, ignorarId)) return r;
    }
}

/* Pergunta a quantidade de cada item ativo, mostrando o disponivel. */
vector<Qtd> lerItensDoPedido(const string& ini, const string& fim) {
    vector<Qtd> v;
    cout << "Itens (Enter = 0). Disponivel no periodo entre colchetes.\n";
    for (size_t i = 0; i < itens.size(); i++) {
        if (!itens[i].ativo) continue;
        int disp = disponivelNoPeriodo(itens[i].id, ini, fim, 0);
        int q = lerInteiroPadrao("  " + col(itens[i].nome, 18) + "[" + to_string(disp) + "]: ",
                                 0, 999999, 0);
        if (q > 0) { Qtd x; x.itemId = itens[i].id; x.qtd = q; v.push_back(x); }
    }
    return v;
}

/* Grava a retirada ligada a uma entrega: mesmos itens, endereco e bairro.
   So pede data e horario (obrigatorios) e a rota (opcional). */
int criarRetirada(int idEntrega) {
    int ie = indiceServico(idEntrega);
    if (ie < 0) return 0;
    Servico r = servicos[ie];
    cout << "\n--- Retirada de " << nomeCliente(r.clienteId) << " (entrega em " << r.data << ") ---\n";
    while (true) {
        r.data = lerData("Data da retirada", false);
        if (chaveData(r.data) >= chaveData(servicos[ie].data)) break;
        cout << "  ! A retirada nao pode ser antes da entrega.\n";
    }
    r.horario = lerHorario("Horario da retirada: ", true);
    r.rota = lerRota("Rota da retirada (ex.: B1, Enter = definir depois): ", r.data, 0, "");
    r.id = proximoIdServico();
    r.tipo = 'R';
    r.status = "PENDENTE";
    r.motorista = "";
    r.obs = "";
    r.vinculo = idEntrega;
    Trava t;
    servicos.push_back(r);
    servicos[indiceServico(idEntrega)].vinculo = r.id;
    salvarServicos();
    return r.id;
}

void novoPedido() {
    cout << "\n=== NOVO PEDIDO (entrega + retirada) ===\n";
    int cid = selecionarCliente();
    if (cid == 0) return;
    const Cliente cli = clientes[indiceCliente(cid)];

    Servico e;
    e.clienteId = cid;
    e.tipo = 'E';
    e.data = lerData("Data da entrega", false);
    e.horario = lerHorario("Horario da entrega (Enter = sem horario): ", false);

    Servico r;
    while (true) {
        r.data = lerData("Data da retirada", false);
        if (chaveData(r.data) >= chaveData(e.data)) break;
        cout << "  ! A retirada nao pode ser antes da entrega.\n";
    }
    r.horario = lerHorario("Horario da retirada: ", true);

    while (true) {
        e.itens = lerItensDoPedido(e.data, r.data);
        e.outros = lerTexto("Servico extra / observacao dos itens (opcional): ", false);
        if (!e.itens.empty() || !e.outros.empty()) break;
        cout << "  ! Informe pelo menos um item ou descreva o servico.\n";
    }
    if (!conferirEstoque(e.itens, e.data, r.data, 0) &&
        !lerSimNao("Falta item no periodo. Registrar o pedido mesmo assim?")) {
        cout << "Pedido nao registrado.\n";
        return;
    }

    cout << "Local (digite DEPOSITO se o cliente busca e devolve no deposito).\n";
    string b = lerLinha("Bairro [Enter = " + cli.bairro + "]: ");
    e.bairro = b.empty() ? cli.bairro : maiusculas(b);
    if (e.bairro == "DEPOSITO") e.endereco = "";
    else {
        string en = lerLinha("Endereco [Enter = " + (cli.endereco.empty() ? string("nao informado") : cli.endereco) + "]: ");
        e.endereco = en.empty() ? cli.endereco : en;
    }
    e.rota = lerRota("Rota da entrega (ex.: A1, Enter = definir depois): ", e.data, 0, "");
    r.rota = lerRota("Rota da retirada (ex.: B1, Enter = definir depois): ", r.data, 0, "");
    e.obs = lerTexto("Observacao (opcional): ", false);

    cout << "\nResumo: " << cli.nome << " | " << textoItens(e) << '\n'
         << "  Entrega  " << e.data << ' ' << (e.horario.empty() ? "--:--" : e.horario)
         << "  rota " << (e.rota.empty() ? "-" : e.rota) << '\n'
         << "  Retirada " << r.data << ' ' << r.horario
         << "  rota " << (r.rota.empty() ? "-" : r.rota) << '\n'
         << "  Local: " << e.bairro << (e.endereco.empty() ? "" : " - " + e.endereco) << '\n';
    if (!lerSimNao("Confirmar pedido?")) { cout << "Pedido nao registrado.\n"; return; }

    e.id = proximoIdServico();
    e.status = "PENDENTE";
    e.motorista = "";
    r.id = e.id + 1;
    r.clienteId = cid;
    r.tipo = 'R';
    r.itens = e.itens;
    r.outros = e.outros;
    r.endereco = e.endereco;
    r.bairro = e.bairro;
    r.status = "PENDENTE";
    r.motorista = "";
    r.obs = "";
    e.vinculo = r.id;
    r.vinculo = e.id;
    {
        Trava t;
        servicos.push_back(e);
        servicos.push_back(r);
        salvarServicos();
    }
    cout << "Pedido registrado: entrega ID " << e.id << " e retirada ID " << r.id << ".\n";
}

/* ======================================================================
 *                  PROGRAMACAO DO DIA / ROTEIRO (RF07-RF08)
 * ====================================================================== */
string gerarRoteiro(const string& data) {
    vector<Servico> dia;
    for (size_t i = 0; i < servicos.size(); i++)
        if (servicos[i].data == data && servicos[i].status != "CANCELADO")
            dia.push_back(servicos[i]);
    sort(dia.begin(), dia.end(), vemAntes);

    ostringstream o;
    o << "\n=====================================================================\n"
      << " PROGRAMACAO DE " << diaSemana(data) << " " << data << '\n'
      << "=====================================================================\n";
    if (dia.empty()) { o << "Nada agendado para este dia.\n"; return o.str(); }

    char viagemAtual = 0;
    int ent = 0, entPend = 0, ret = 0, retPend = 0;
    vector<int> sai(itens.size(), 0), volta(itens.size(), 0);
    o << cabecalhoServicos(false);
    for (size_t i = 0; i < dia.size(); i++) {
        const Servico& s = dia[i];
        char viagem = s.rota.empty() ? '-' : s.rota[0];
        if (viagem != viagemAtual) {
            o << (viagem == '-' ? "--- SEM ROTA DEFINIDA ---\n"
                                : string("--- VIAGEM ") + viagem + " ---\n");
            viagemAtual = viagem;
        }
        o << linhaServico(s, false, true);
        if (s.tipo == 'E') { ent++; if (s.status == "PENDENTE") entPend++; }
        else { ret++; if (s.status == "PENDENTE") retPend++; }
        for (size_t k = 0; k < itens.size(); k++)
            (s.tipo == 'E' ? sai : volta)[k] += qtdDoItem(s, itens[k].id);
    }
    o << string(89, '-') << '\n'
      << "Entregas: " << ent << " (pendentes: " << entPend << ")   "
      << "Retiradas: " << ret << " (pendentes: " << retPend << ")\n";
    string ts, tv;
    for (size_t k = 0; k < itens.size(); k++) {
        if (sai[k]) ts += (ts.empty() ? "" : ", ") + to_string(sai[k]) + " " + itens[k].nome;
        if (volta[k]) tv += (tv.empty() ? "" : ", ") + to_string(volta[k]) + " " + itens[k].nome;
    }
    if (!ts.empty()) o << "Saem do deposito: " << ts << '\n';
    if (!tv.empty()) o << "Voltam ao deposito: " << tv << '\n';
    o << "[ ] pendente   [X] feito\n";
    return o.str();
}

void programacaoDoDia() {
    string data = lerData("\nData da programacao", true);
    string texto = gerarRoteiro(data);
    cout << texto;
    if (lerSimNao("Gerar arquivo TXT para imprimir?")) {
        string nome = "roteiro_" + data.substr(6, 4) + "-" + data.substr(3, 2) + "-" +
                      data.substr(0, 2) + ".txt";
        ofstream f(nome.c_str());
        f << texto;
        cout << "Arquivo gerado: " << nome << '\n';
    }
}

/* ======================================================================
 *                 BAIXA, REAGENDAMENTO E CANCELAMENTO (RF09-RF11)
 * ====================================================================== */
void darBaixa() {
    static string ultimoMotorista = "";
    string data = lerData("\nData das paradas", true);
    vector<Servico> pend;
    for (size_t i = 0; i < servicos.size(); i++)
        if (servicos[i].data == data && servicos[i].status == "PENDENTE")
            pend.push_back(servicos[i]);
    sort(pend.begin(), pend.end(), vemAntes);
    if (pend.empty()) { cout << "Nenhuma parada pendente nesse dia.\n"; return; }

    while (true) {
        cout << '\n' << cabecalhoServicos(false);
        for (size_t i = 0; i < pend.size(); i++) cout << linhaServico(pend[i], false, false);
        int id = lerInteiro("ID para dar baixa (0 = voltar): ", 0, 999999999);
        if (id == 0) return;
        int idx = indiceServico(id);
        if (idx < 0 || servicos[idx].status != "PENDENTE" || servicos[idx].data != data) {
            cout << "  ! ID nao esta na lista.\n";
            continue;
        }
        if (servicos[idx].tipo == 'R' && servicos[idx].vinculo != 0) {
            int iv = indiceServico(servicos[idx].vinculo);
            if (iv >= 0 && servicos[iv].status == "PENDENTE")
                cout << "  ! Atencao: a entrega deste pedido ainda esta pendente.\n";
        }
        string m = lerLinha("Motorista" + (ultimoMotorista.empty() ? string("") :
                            " [Enter = " + ultimoMotorista + "]") + ": ");
        if (m.empty()) m = ultimoMotorista;
        string obs = lerTexto("Observacao da baixa (opcional): ", false);
        if (!lerSimNao("Confirmar baixa de " + nomeCliente(servicos[idx].clienteId) + "?")) continue;
        Trava t;
        if (servicos[idx].status != "PENDENTE") {
            cout << "  ! Essa parada acabou de receber baixa pelo celular.\n";
            continue;
        }
        Servico& s = servicos[idx];
        s.status = "FEITO";
        s.motorista = m;
        if (!obs.empty()) s.obs = s.obs.empty() ? obs : s.obs + " / " + obs;
        ultimoMotorista = m;
        salvarServicos();
        cout << "Baixa registrada.\n";
        for (size_t i = 0; i < pend.size(); i++)
            if (pend[i].id == id) { pend.erase(pend.begin() + i); break; }
        if (pend.empty()) { cout << "Todas as paradas do dia foram concluidas.\n"; return; }
    }
}

int pedirServico(const string& prompt) {
    int id = lerInteiro(prompt, 0, 999999999);
    if (id == 0) return -1;
    int idx = indiceServico(id);
    if (idx < 0) { cout << "  ! Agendamento nao encontrado.\n"; return -1; }
    cout << cabecalhoServicos(true) << linhaServico(servicos[idx], true, true);
    return idx;
}

void desfazerBaixa() {
    int idx = pedirServico("\nID do agendamento (0 = voltar): ");
    if (idx < 0) return;
    if (servicos[idx].status != "FEITO") { cout << "  ! Este agendamento nao esta como FEITO.\n"; return; }
    if (lerSimNao("Voltar para PENDENTE?")) {
        Trava t;
        servicos[idx].status = "PENDENTE";
        servicos[idx].motorista = "";
        salvarServicos();
        cout << "Baixa desfeita.\n";
    }
}

void reagendar() {
    int idx = pedirServico("\nID do agendamento (0 = voltar): ");
    if (idx < 0) return;
    if (servicos[idx].status == "CANCELADO") { cout << "  ! Agendamento cancelado.\n"; return; }
    bool ehRetirada = servicos[idx].tipo == 'R';
    cout << "Enter mantem o valor atual.\n";
    string d = lerLinha("Nova data [" + servicos[idx].data + "]: ");
    string novaData = servicos[idx].data;
    if (!d.empty()) {
        novaData = normalizarData(d);
        if (novaData.empty()) { cout << "  ! Data invalida. Nada foi alterado.\n"; return; }
    }
    string h = lerLinha("Novo horario [" + servicos[idx].horario + "]" +
                        (ehRetirada ? "" : " (- = sem horario)") + ": ");
    string novoHorario = servicos[idx].horario;
    if (h == "-" && !ehRetirada) novoHorario = "";
    else if (!h.empty()) {
        novoHorario = normalizarHorario(h);
        if (novoHorario == "?" || novoHorario.empty()) { cout << "  ! Horario invalido. Nada foi alterado.\n"; return; }
    }
    if (ehRetirada && novoHorario.empty()) { cout << "  ! A retirada precisa de horario. Nada foi alterado.\n"; return; }
    string novaRota = lerRota("Nova rota [" + servicos[idx].rota + "]: ", novaData,
                              servicos[idx].id, servicos[idx].rota);

    /* Periodo do pedido depois da mudanca, para validar ordem e estoque. */
    int iv = servicos[idx].vinculo ? indiceServico(servicos[idx].vinculo) : -1;
    int idEntrega = ehRetirada ? (iv >= 0 ? servicos[iv].id : 0) : servicos[idx].id;
    string ini = ehRetirada ? (iv >= 0 ? servicos[iv].data : novaData) : novaData;
    string fim = ehRetirada ? novaData : (iv >= 0 ? servicos[iv].data : novaData);
    if (iv >= 0 && servicos[iv].status != "CANCELADO" && chaveData(fim) < chaveData(ini)) {
        cout << "  ! A retirada ficaria antes da entrega. Nada foi alterado.\n";
        return;
    }
    if (novaData != servicos[idx].data && idEntrega != 0) {
        int ie = indiceServico(idEntrega);
        if (!conferirEstoque(servicos[ie].itens, ini, fim, idEntrega) &&
            !lerSimNao("Falta item no novo periodo. Alterar mesmo assim?")) {
            cout << "Nada foi alterado.\n";
            return;
        }
    }
    Trava t;
    servicos[idx].data = novaData;
    servicos[idx].horario = novoHorario;
    servicos[idx].rota = novaRota;
    salvarServicos();
    cout << "Agendamento atualizado.\n";
}

/* Cancelar uma parte do pedido cancela o pedido inteiro. */
void cancelarPedido() {
    int idx = pedirServico("\nID da entrega ou retirada (0 = voltar): ");
    if (idx < 0) return;
    if (servicos[idx].status == "CANCELADO") { cout << "  ! Ja esta cancelado.\n"; return; }
    int iv = servicos[idx].vinculo ? indiceServico(servicos[idx].vinculo) : -1;
    if (servicos[idx].status == "FEITO") {
        cout << "  ! Este agendamento ja teve baixa. Desfaca a baixa antes de cancelar.\n";
        return;
    }
    if (iv >= 0 && servicos[iv].status == "FEITO") {
        cout << "  ! A outra parte do pedido ja teve baixa. Use \"Alterar data\" em vez de cancelar.\n";
        return;
    }
    if (!lerSimNao(iv >= 0 ? "Cancelar o pedido inteiro (entrega e retirada)?" : "Cancelar este agendamento?")) return;
    Trava t;
    servicos[idx].status = "CANCELADO";
    if (iv >= 0) servicos[iv].status = "CANCELADO";
    salvarServicos();
    cout << "Cancelado. Os itens voltam a ficar livres no periodo.\n";
}

void menuAgendamentos() {
    while (true) {
        cout << "\n----- BAIXA E ALTERACOES -----\n"
                "1 - Dar baixa (marcar como feito)\n"
                "2 - Desfazer baixa\n"
                "3 - Alterar data, horario ou rota\n"
                "4 - Cancelar pedido\n"
                "0 - Voltar\n";
        int op = lerInteiro("Opcao: ", 0, 4);
        if (op == 0) return;
        if (op == 1) darBaixa();
        if (op == 2) desfazerBaixa();
        if (op == 3) reagendar();
        if (op == 4) cancelarPedido();
    }
}

/* ======================================================================
 *                         CONSULTAS E RELATORIOS (RF12-RF15)
 * ====================================================================== */
void listarFiltrados(vector<Servico> lista, const string& vazio) {
    if (lista.empty()) { cout << vazio << '\n'; return; }
    sort(lista.begin(), lista.end(), vemAntesComData);
    cout << cabecalhoServicos(true);
    for (size_t i = 0; i < lista.size(); i++) cout << linhaServico(lista[i], true, true);
    cout << lista.size() << " registro(s).\n";
}

void relatorioAtrasados() {
    int h = chaveData(hoje());
    vector<Servico> l;
    for (size_t i = 0; i < servicos.size(); i++)
        if (servicos[i].status == "PENDENTE" && chaveData(servicos[i].data) < h)
            l.push_back(servicos[i]);
    cout << "\n=== PENDENCIAS DE DIAS ANTERIORES (nao receberam baixa) ===\n";
    listarFiltrados(l, "Nenhuma pendencia atrasada.");
}

void relatorioProximosDias() {
    string inicio = hoje();
    cout << "\n=== PROXIMOS 7 DIAS ===\n"
         << col("DIA", 8) << col("DATA", 10) << col("ENTREG.", 7) << col("RETIR.", 7) << "ITENS QUE SAEM\n"
         << string(78, '-') << '\n';
    for (int d = 0; d < 7; d++) {
        string data = somarDias(inicio, d);
        int e = 0, r = 0;
        vector<int> sai(itens.size(), 0);
        for (size_t i = 0; i < servicos.size(); i++) {
            const Servico& s = servicos[i];
            if (s.data != data || s.status == "CANCELADO") continue;
            if (s.tipo == 'E') {
                e++;
                for (size_t k = 0; k < itens.size(); k++) sai[k] += qtdDoItem(s, itens[k].id);
            } else r++;
        }
        string ts;
        for (size_t k = 0; k < itens.size(); k++)
            if (sai[k]) ts += (ts.empty() ? "" : ", ") + to_string(sai[k]) + " " + itens[k].nome;
        cout << col(diaSemana(data), 8) << col(data, 10) << col(to_string(e), 7)
             << col(to_string(r), 7) << (ts.empty() ? "-" : ts) << '\n';
    }
}

void relatorioCliente() {
    int cid = selecionarCliente();
    if (cid == 0) return;
    vector<Servico> l;
    for (size_t i = 0; i < servicos.size(); i++)
        if (servicos[i].clienteId == cid) l.push_back(servicos[i]);
    cout << "\n=== HISTORICO DE " << nomeCliente(cid) << " ===\n";
    listarFiltrados(l, "Nenhum agendamento para este cliente.");
}

/* Entregas antigas (da versao 1) que ficaram sem retirada. */
void relatorioSemRetirada() {
    vector<Servico> l;
    for (size_t i = 0; i < servicos.size(); i++) {
        const Servico& s = servicos[i];
        if (s.tipo != 'E' || s.status == "CANCELADO") continue;
        int iv = s.vinculo ? indiceServico(s.vinculo) : -1;
        if (iv < 0 || servicos[iv].status == "CANCELADO") l.push_back(s);
    }
    cout << "\n=== ENTREGAS SEM RETIRADA ===\n";
    listarFiltrados(l, "Todas as entregas tem retirada.");
    if (l.empty()) return;
    int id = lerInteiro("ID da entrega para criar a retirada (0 = voltar): ", 0, 999999999);
    if (id == 0) return;
    for (size_t i = 0; i < l.size(); i++)
        if (l[i].id == id) {
            int rid = criarRetirada(id);
            if (rid) cout << "Retirada criada com ID " << rid << ".\n";
            return;
        }
    cout << "  ! ID nao esta na lista.\n";
}

void menuRelatorios() {
    while (true) {
        cout << "\n----- CONSULTAS E RELATORIOS -----\n"
                "1 - Pendencias de dias anteriores\n"
                "2 - Resumo dos proximos 7 dias\n"
                "3 - Historico de um cliente\n"
                "4 - Entregas sem retirada (dados antigos)\n"
                "0 - Voltar\n";
        int op = lerInteiro("Opcao: ", 0, 4);
        if (op == 0) return;
        if (op == 1) relatorioAtrasados();
        if (op == 2) relatorioProximosDias();
        if (op == 3) relatorioCliente();
        if (op == 4) relatorioSemRetirada();
    }
}

/* ======================================================================
 *                 PAINEL HTML E SERVIDOR PARA CELULARES (RF20-RF22)
 * ----------------------------------------------------------------------
 *  - painel.html: gerado a cada gravacao. Abre em qualquer navegador e
 *    mostra a programacao do dia, os alertas e o estoque (so leitura).
 *  - Servidor: roda em segundo plano enquanto o menu continua livre.
 *    Os celulares no mesmo Wi-Fi abrem http://IP-DO-PC:8080 e dao baixa
 *    nas paradas. A pagina se atualiza sozinha a cada 30 segundos.
 * ====================================================================== */

/* ---------- texto seguro para HTML ---------- */
bool utf8Valido(const string& s) {
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = (unsigned char)s[i];
        int n;
        if (c < 0x80) n = 0;
        else if ((c & 0xE0) == 0xC0) n = 1;
        else if ((c & 0xF0) == 0xE0) n = 2;
        else if ((c & 0xF8) == 0xF0) n = 3;
        else return false;
        if (n > 0 && i + n >= s.size()) return false;
        for (int k = 1; k <= n; k++)
            if (((unsigned char)s[i + k] & 0xC0) != 0x80) return false;
        i += n + 1;
    }
    return true;
}

/* O console do Windows grava acentos na pagina de codigo 850; o navegador
   espera UTF-8. Converte as letras usadas em portugues. */
string paraUtf8(const string& s) {
    if (utf8Valido(s)) return s;
    static const unsigned char de[] = {0xA0,0x82,0xA1,0xA2,0xA3,0x83,0x88,0x93,0xC6,0xE4,0x87,0x85,0x81,
                                       0xB5,0x90,0xD6,0xE0,0xE9,0xB6,0xD2,0xE2,0xC7,0xE5,0x80,0xB7};
    static const char* para[] = {"\xC3\xA1","\xC3\xA9","\xC3\xAD","\xC3\xB3","\xC3\xBA","\xC3\xA2","\xC3\xAA",
                                 "\xC3\xB4","\xC3\xA3","\xC3\xB5","\xC3\xA7","\xC3\xA0","\xC3\xBC",
                                 "\xC3\x81","\xC3\x89","\xC3\x8D","\xC3\x93","\xC3\x9A","\xC3\x82","\xC3\x8A",
                                 "\xC3\x94","\xC3\x83","\xC3\x95","\xC3\x87","\xC3\x80"};
    string r;
    for (size_t i = 0; i < s.size(); i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80) { r += (char)c; continue; }
        bool achou = false;
        for (size_t k = 0; k < sizeof de; k++)
            if (de[k] == c) { r += para[k]; achou = true; break; }
        if (!achou) r += '?';
    }
    return r;
}

string h(const string& s) {
    string t = paraUtf8(s), r;
    for (size_t i = 0; i < t.size(); i++) {
        char c = t[i];
        if (c == '&') r += "&amp;"; else if (c == '<') r += "&lt;"; else if (c == '>') r += "&gt;";
        else if (c == '"') r += "&quot;"; else r += c;
    }
    return r;
}

/* DD/MM/AAAA <-> DD-MM-AAAA (formato usado nos links) */
string dataParaLink(const string& d) { string r = d; replace(r.begin(), r.end(), '/', '-'); return r; }
string linkParaData(const string& d) { string r = d; replace(r.begin(), r.end(), '-', '/'); return dataValida(r) ? r : hoje(); }

const char* CSS_PAINEL = R"CSS(
:root{--bg:#eef1f4;--card:#fff;--card2:#f6f8fa;--line:#d6dce3;--ink:#16202b;--muted:#5b6876;--accent:#c97a00;--accent-ink:#1b1300;--soft:#fff1d6;
--ok:#1f7a4a;--ok-soft:#dcf2e5;--ok-ink:#fff;--warn:#a85c00;--warn-soft:#fde9cc;--bad:#b3261e;--bad-soft:#fbe2df;--ent:#1d5fa8;--ent-soft:#dde9f7;--ret:#6b3fa0;--ret-soft:#ece3f7}
@media (prefers-color-scheme:dark){:root{--bg:#10161d;--card:#18212b;--card2:#1d2833;--line:#2b3845;--ink:#e6ecf2;--muted:#93a1b0;--accent:#f0a21c;--soft:#3a2a0a;
--ok:#5ccf8e;--ok-soft:#173326;--ok-ink:#06210f;--warn:#f2b45a;--warn-soft:#3a2a12;--bad:#ff8a80;--bad-soft:#3d1d1b;--ent:#7fb2f0;--ent-soft:#18283b;--ret:#c3a2ee;--ret-soft:#2a2038;color-scheme:dark}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.45 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
.wrap{max-width:1100px;margin:0 auto;padding:0 16px}
header{background:var(--card);border-bottom:1px solid var(--line);position:sticky;top:0;z-index:2}
header .wrap{display:flex;flex-wrap:wrap;gap:8px 16px;align-items:center;padding-block:10px}
h1{margin:0 auto 0 0;font:700 21px/1 "Arial Narrow",Arial,sans-serif;text-transform:uppercase;letter-spacing:.02em;display:flex;gap:10px;align-items:center}
.sign{display:inline-grid;place-items:center;min-width:34px;height:26px;padding:0 6px;border-radius:5px;background:var(--accent);color:var(--accent-ink);font:700 13px ui-monospace,Consolas,monospace;border:2px solid var(--accent-ink)}
.sign.none{background:var(--card2);color:var(--muted);border-color:var(--line)}
.nav{display:flex;gap:6px;align-items:center}
.btn{display:inline-flex;align-items:center;justify-content:center;border:1px solid var(--line);background:var(--card);color:var(--ink);border-radius:8px;padding:7px 12px;font:600 14px inherit;text-decoration:none;cursor:pointer}
.btn.ok{background:var(--ok);border-color:var(--ok);color:var(--ok-ink);width:100%;padding:12px;font-size:16px}
.btn.sm{padding:4px 9px;font-size:13px}
main.wrap{padding-top:16px;padding-bottom:40px;display:grid;gap:16px}
.day{display:flex;flex-wrap:wrap;align-items:baseline;gap:4px 12px}.day h2{margin:0;font:700 28px/1 "Arial Narrow",Arial,sans-serif;text-transform:uppercase}.day span{color:var(--muted)}
.msg{border-radius:9px;padding:9px 12px;font-weight:600}.msg.ok{background:var(--ok-soft);color:var(--ok)}.msg.bad{background:var(--bad-soft);color:var(--bad)}
.kpis{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:10px}
.kpi{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:10px 12px}.kpi small{color:var(--muted);font-size:12px;text-transform:uppercase;letter-spacing:.05em}
.kpi b{display:block;font:700 28px/1.1 "Arial Narrow",Arial,sans-serif}.kpi.hot{border-color:var(--bad);background:var(--bad-soft)}.kpi.hot b{color:var(--bad)}
.cols{display:grid;grid-template-columns:minmax(0,1fr) 320px;gap:16px;align-items:start}@media(max-width:860px){.cols{grid-template-columns:minmax(0,1fr)}}
.trip{background:var(--card);border:1px solid var(--line);border-radius:12px;overflow:hidden}.trip+.trip{margin-top:12px}
.trip>h3{margin:0;display:flex;gap:10px;align-items:center;padding:9px 12px;background:var(--card2);border-bottom:1px solid var(--line);font:700 17px "Arial Narrow",Arial,sans-serif;text-transform:uppercase}
.trip>h3 small{margin-left:auto;font:400 13px system-ui,sans-serif;color:var(--muted);text-transform:none}
.stop{display:grid;grid-template-columns:auto minmax(0,1fr) auto;gap:6px 12px;padding:12px;border-top:1px solid var(--line)}.stop:first-of-type{border-top:0}
.stop>.pill{align-self:start}.stop .who b{font-size:16px}.stop .who small{display:block;color:var(--muted)}.stop.feito{opacity:.6}.stop.feito .who b{text-decoration:line-through}
.stop .its{grid-column:2/-1;font:600 14px ui-monospace,Consolas,monospace}.stop .extra{grid-column:2/-1;font-size:13px;color:var(--muted)}
.stop form,.stop .done{grid-column:1/-1}.done{display:flex;gap:8px;align-items:center;font-size:13px;color:var(--muted)}
.pill{display:inline-block;padding:2px 8px;border-radius:999px;font-size:12px;font-weight:700;white-space:nowrap}
.E{background:var(--ent-soft);color:var(--ent)}.R{background:var(--ret-soft);color:var(--ret)}
.OK,.FEITO{background:var(--ok-soft);color:var(--ok)}.BAIXO,.PENDENTE{background:var(--warn-soft);color:var(--warn)}.FALTA{background:var(--bad-soft);color:var(--bad)}
.panel{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:12px}.panel+.panel{margin-top:12px}
.panel h4{margin:0 0 8px;font:700 16px "Arial Narrow",Arial,sans-serif;text-transform:uppercase;color:var(--muted);letter-spacing:.04em}
table{width:100%;border-collapse:collapse;font-size:14px}td,th{padding:6px 4px;border-bottom:1px solid var(--line);text-align:left}th{font-size:11.5px;color:var(--muted);text-transform:uppercase}
td.n{text-align:right;font-family:ui-monospace,Consolas,monospace}
.me{display:flex;flex-wrap:wrap;gap:8px;align-items:center;background:var(--card);border:1px solid var(--line);border-radius:10px;padding:10px 12px}
.me input{border:1px solid var(--line);background:var(--card2);color:var(--ink);border-radius:8px;padding:8px 10px;font:inherit;flex:1 1 140px;min-width:0}
.empty{padding:24px;text-align:center;color:var(--muted)}footer{color:var(--muted);font-size:12.5px;text-align:center;padding-bottom:20px}
)CSS";

/* Monta a pagina do dia. 'servidor' liga os botoes de baixa. */
string htmlPainel(const string& dia, bool servidor, const string& motorista, const string& msg, bool msgErro) {
    ostringstream o;
    vector<Servico> lista;
    for (size_t i = 0; i < servicos.size(); i++)
        if (servicos[i].data == dia && servicos[i].status != "CANCELADO") lista.push_back(servicos[i]);
    sort(lista.begin(), lista.end(), vemAntes);
    int feitos = 0, ent = 0, ret = 0;
    for (size_t i = 0; i < lista.size(); i++) {
        if (lista[i].status == "FEITO") feitos++;
        if (lista[i].tipo == 'E') ent++; else ret++;
    }
    int alertas = contarAlertas();
    string link = "/?d=";

    o << "<!doctype html><html lang=\"pt-BR\"><head><meta charset=\"utf-8\">"
         "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">";
    if (servidor) o << "<meta http-equiv=\"refresh\" content=\"30;url=" << link << dataParaLink(dia) << "\">";
    o << "<title>Painel de Rotas</title><style>" << CSS_PAINEL << "</style></head><body>"
      << "<header><div class=\"wrap\"><h1><span class=\"sign\">A1</span>Painel de Rotas</h1>";
    if (servidor)
        o << "<nav class=\"nav\"><a class=\"btn sm\" href=\"" << link << dataParaLink(somarDias(dia, -1)) << "\">&lsaquo;</a>"
          << "<a class=\"btn sm\" href=\"/\">Hoje</a>"
          << "<a class=\"btn sm\" href=\"" << link << dataParaLink(somarDias(dia, 1)) << "\">&rsaquo;</a></nav>";
    o << "</div></header><main class=\"wrap\">";

    o << "<div class=\"day\"><h2>" << diaSemana(dia) << " " << dia.substr(0, 5) << "</h2><span>"
      << (dia == hoje() ? "hoje" : dia) << "</span></div>";
    if (!msg.empty()) o << "<div class=\"msg " << (msgErro ? "bad" : "ok") << "\">" << msg << "</div>";
    if (servidor)
        o << "<form class=\"me\" method=\"post\" action=\"/nome\"><input type=\"hidden\" name=\"d\" value=\""
          << dataParaLink(dia) << "\"><b>Motorista</b><input name=\"nome\" value=\"" << h(motorista)
          << "\" placeholder=\"Seu nome\" maxlength=\"40\"><button class=\"btn\">Salvar</button></form>";

    o << "<div class=\"kpis\"><div class=\"kpi\"><small>Paradas feitas</small><b>" << feitos << "/" << lista.size() << "</b></div>"
      << "<div class=\"kpi\"><small>Entregas</small><b>" << ent << "</b></div>"
      << "<div class=\"kpi\"><small>Retiradas</small><b>" << ret << "</b></div>"
      << "<div class=\"kpi" << (alertas ? " hot" : "") << "\"><small>Alertas de estoque</small><b>" << alertas << "</b></div></div>";

    o << "<div class=\"cols\"><div>";
    if (lista.empty()) o << "<div class=\"trip\"><div class=\"empty\">Nada agendado para este dia.</div></div>";
    char viagemAtual = 0;
    for (size_t i = 0; i < lista.size(); i++) {
        const Servico& s = lista[i];
        char viagem = s.rota.empty() ? '-' : s.rota[0];
        if (viagem != viagemAtual) {
            if (viagemAtual) o << "</div>";
            int tot = 0, fe = 0;
            for (size_t k = 0; k < lista.size(); k++) {
                char vk = lista[k].rota.empty() ? '-' : lista[k].rota[0];
                if (vk == viagem) { tot++; if (lista[k].status == "FEITO") fe++; }
            }
            o << "<div class=\"trip\"><h3>" << (viagem == '-' ? "<span class=\"sign none\">?</span>Sem rota definida"
                                                              : string("<span class=\"sign\">") + viagem + "</span>Viagem " + viagem)
              << "<small>" << fe << "/" << tot << " feitas</small></h3>";
            viagemAtual = viagem;
        }
        int ic = indiceCliente(s.clienteId);
        string tel = ic >= 0 ? clientes[ic].telefone : "";
        o << "<div class=\"stop" << (s.status == "FEITO" ? " feito" : "") << "\">"
          << (s.rota.empty() ? "<span class=\"sign none\">-</span>" : "<span class=\"sign\">" + h(s.rota) + "</span>")
          << "<div class=\"who\"><b>" << h(nomeCliente(s.clienteId)) << "</b><small>"
          << (s.horario.empty() ? "sem hor&aacute;rio" : s.horario) << " &middot; " << h(s.bairro)
          << (s.endereco.empty() ? "" : " &middot; " + h(s.endereco)) << "</small></div>"
          << "<span class=\"pill " << s.tipo << "\">" << (s.tipo == 'E' ? "Entrega" : "Retirada") << "</span>"
          << "<div class=\"its\">" << h(textoItens(s)) << "</div>";
        string extra;
        if (!tel.empty()) extra += "Tel. " + h(tel);
        if (!s.obs.empty()) extra += (extra.empty() ? "" : " &middot; ") + h(s.obs);
        if (!extra.empty()) o << "<div class=\"extra\">" << extra << "</div>";
        if (s.status == "FEITO") {
            o << "<div class=\"done\"><span class=\"pill FEITO\">Feito</span>"
              << (s.motorista.empty() ? "" : "por " + h(s.motorista));
            if (servidor)
                o << "<form method=\"post\" action=\"/desfazer\" style=\"margin-left:auto\"><input type=\"hidden\" name=\"id\" value=\""
                  << s.id << "\"><input type=\"hidden\" name=\"d\" value=\"" << dataParaLink(dia)
                  << "\"><button class=\"btn sm\">Desfazer</button></form>";
            o << "</div>";
        } else if (servidor) {
            o << "<form method=\"post\" action=\"/baixa\"><input type=\"hidden\" name=\"id\" value=\"" << s.id
              << "\"><input type=\"hidden\" name=\"d\" value=\"" << dataParaLink(dia) << "\"><button class=\"btn ok\">&check; "
              << (s.tipo == 'E' ? "Entregue" : "Retirado") << "</button></form>";
        } else {
            o << "<div class=\"done\"><span class=\"pill PENDENTE\">Pendente</span></div>";
        }
        o << "</div>";
    }
    if (viagemAtual) o << "</div>";
    o << "</div><aside>";

    /* estoque */
    string ini = hoje(), fim = somarDias(hoje(), 6);
    o << "<div class=\"panel\"><h4>Estoque (pr&oacute;ximos 7 dias)</h4><table><tr><th>Item</th><th>Livre hoje</th><th>Menor</th><th></th></tr>";
    for (size_t i = 0; i < itens.size(); i++) {
        const Item& it = itens[i];
        if (!it.ativo) continue;
        string d;
        int menor = it.total - picoNoPeriodo(it.id, ini, fim, 0, &d);
        string sit = menor < 0 ? "FALTA" : (menor < it.minimo ? "BAIXO" : "OK");
        o << "<tr><td>" << h(it.nome) << "</td><td class=\"n\">" << it.total - comprometido(it.id, ini, 0)
          << "/" << it.total << "</td><td class=\"n\">" << menor << " <small>" << d.substr(0, 5) << "</small></td>"
          << "<td><span class=\"pill " << sit << "\">" << (sit == "OK" ? "OK" : sit == "BAIXO" ? "Baixo" : "Falta")
          << "</span></td></tr>";
    }
    o << "</table></div>";

    /* proximos dias */
    o << "<div class=\"panel\"><h4>Pr&oacute;ximos 7 dias</h4><table><tr><th>Dia</th><th>Entregas</th><th>Retiradas</th></tr>";
    for (int k = 0; k < 7; k++) {
        string d = somarDias(hoje(), k);
        int e = 0, r = 0;
        for (size_t i = 0; i < servicos.size(); i++)
            if (servicos[i].data == d && servicos[i].status != "CANCELADO") (servicos[i].tipo == 'E' ? e : r)++;
        o << "<tr><td>" << (servidor ? "<a href=\"" + link + dataParaLink(d) + "\">" : "")
          << diaSemana(d).substr(0, 3) << " " << d.substr(0, 5) << (servidor ? "</a>" : "")
          << "</td><td class=\"n\">" << e << "</td><td class=\"n\">" << r << "</td></tr>";
    }
    o << "</table></div>";

    int atrasados = 0;
    for (size_t i = 0; i < servicos.size(); i++)
        if (servicos[i].status == "PENDENTE" && chaveData(servicos[i].data) < chaveData(hoje())) atrasados++;
    if (atrasados)
        o << "<div class=\"panel\"><h4>Aten&ccedil;&atilde;o</h4>" << atrasados
          << " parada(s) de dias anteriores sem baixa.</div>";
    o << "</aside></div></main><footer>Gerado pelo programa em C++ em " << hoje() << " &middot; "
      << (servidor ? "atualiza sozinho a cada 30 s" : "arquivo somente leitura") << "</footer></body></html>";
    return o.str();
}

void gerarPainelArquivo(const string& dia) {
    ofstream f("painel.html");
    f << htmlPainel(dia, false, "", "", false);
}

void gerarPainelArquivo() { gerarPainelArquivo(hoje()); }

void abrirPainel() {
    string dia = lerData("\nDia do painel", true);
    {
        Trava t;
        gerarPainelArquivo(dia);
    }
#if defined(_WIN32)
    system("start \"\" painel.html");
#elif defined(__APPLE__)
    system("open painel.html");
#else
    if (system("xdg-open painel.html >/dev/null 2>&1") != 0) {}
#endif
    cout << "Arquivo painel.html gerado na pasta do programa. Se o navegador nao abrir,\n"
            "de dois cliques nele. Ele tambem e atualizado sozinho a cada gravacao.\n";
}

/* ---------- servidor HTTP minimo ---------- */
volatile bool servidorLigado = false;
Sock socketServidor = INVALID_SOCKET;
int portaServidor = 8080;
string pinServidor;
string enderecoServidor;

string urlDecode(const string& s) {
    string r;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '+') r += ' ';
        else if (s[i] == '%' && i + 2 < s.size() && isxdigit((unsigned char)s[i + 1]) && isxdigit((unsigned char)s[i + 2])) {
            r += (char)strtol(s.substr(i + 1, 2).c_str(), NULL, 16);
            i += 2;
        } else r += s[i];
    }
    return r;
}

string urlEncode(const string& s) {
    string r;
    const char* hex = "0123456789ABCDEF";
    for (size_t i = 0; i < s.size(); i++) {
        unsigned char c = (unsigned char)s[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.') r += (char)c;
        else { r += '%'; r += hex[c >> 4]; r += hex[c & 15]; }
    }
    return r;
}

/* "a=1&b=2" ou "a=1; b=2" -> valor da chave */
string campo(const string& texto, const string& chave, char sep) {
    vector<string> partes = dividir(texto, sep);
    for (size_t i = 0; i < partes.size(); i++) {
        string p = trim(partes[i]);
        size_t eq = p.find('=');
        if (eq != string::npos && p.substr(0, eq) == chave) return urlDecode(p.substr(eq + 1));
    }
    return "";
}

void enviar(Sock c, const string& status, const string& extraHeaders, const string& corpo) {
    string resp = "HTTP/1.1 " + status + "\r\nContent-Type: text/html; charset=utf-8\r\n"
                  "Cache-Control: no-store\r\nConnection: close\r\nContent-Length: " + to_string(corpo.size()) +
                  "\r\n" + extraHeaders + "\r\n" + corpo;
    size_t enviado = 0;
    while (enviado < resp.size()) {
        int n = send(c, resp.c_str() + enviado, (int)(resp.size() - enviado), FLAG_ENVIO);
        if (n <= 0) break;
        enviado += (size_t)n;
    }
}

void redirecionar(Sock c, const string& destino, const string& cookie) {
    enviar(c, "303 See Other", "Location: " + destino + "\r\n" + cookie, "");
}

string paginaCodigo(bool errou) {
    return string("<!doctype html><html lang=\"pt-BR\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
                  "<title>Painel de Rotas</title><style>") + CSS_PAINEL + "</style></head><body><main class=\"wrap\" style=\"max-width:420px\">"
                  "<div class=\"day\"><h2>Painel de Rotas</h2></div>" +
                  (errou ? "<div class=\"msg bad\">C&oacute;digo incorreto.</div>" : "") +
                  "<form class=\"me\" method=\"post\" action=\"/entrar\"><b>C&oacute;digo de acesso</b>"
                  "<input name=\"pin\" inputmode=\"numeric\" autofocus><button class=\"btn\">Entrar</button></form></main></body></html>";
}

/* Atende uma requisicao. Todo acesso aos dados fica dentro da trava. */
void atender(Sock c) {
    string req;
    char buf[4096];
    size_t fimCab = string::npos;
    while (req.size() < 65536) {
        int n = recv(c, buf, sizeof buf, 0);
        if (n <= 0) break;
        req.append(buf, n);
        fimCab = req.find("\r\n\r\n");
        if (fimCab != string::npos) {
            size_t cl = 0;
            string baixo = req.substr(0, fimCab);
            for (size_t i = 0; i < baixo.size(); i++) baixo[i] = (char)tolower((unsigned char)baixo[i]);
            size_t p = baixo.find("content-length:");
            if (p != string::npos) cl = (size_t)atoi(baixo.substr(p + 15).c_str());
            if (req.size() >= fimCab + 4 + cl) break;
        }
    }
    if (fimCab == string::npos) return;
    string cab = req.substr(0, fimCab), corpo = req.substr(fimCab + 4);
    string metodo = cab.substr(0, cab.find(' '));
    size_t a = cab.find(' ') + 1;
    string alvo = cab.substr(a, cab.find(' ', a) - a);
    string caminho = alvo.substr(0, alvo.find('?'));
    string query = alvo.find('?') == string::npos ? "" : alvo.substr(alvo.find('?') + 1);
    string cookies;
    {
        string baixo = cab;
        for (size_t i = 0; i < baixo.size(); i++) baixo[i] = (char)tolower((unsigned char)baixo[i]);
        size_t p = baixo.find("\r\ncookie:");
        if (p != string::npos) {
            size_t ini = p + 9, fim = cab.find("\r\n", ini);
            cookies = cab.substr(ini, fim == string::npos ? string::npos : fim - ini);
        }
    }
    string motorista = campo(cookies, "mot", ';');
    string dLink = metodo == "POST" ? campo(corpo, "d", '&') : campo(query, "d", '&');
    string dia = dLink.empty() ? hoje() : linkParaData(dLink);
    string volta = "/?d=" + dataParaLink(dia);
    const string umAno = "; Path=/; Max-Age=31536000; SameSite=Lax\r\n";

    if (caminho == "/entrar" && metodo == "POST") {
        string pin = campo(corpo, "pin", '&');
        if (pin == pinServidor) redirecionar(c, "/", "Set-Cookie: pin=" + urlEncode(pin) + umAno);
        else enviar(c, "200 OK", "", paginaCodigo(true));
        return;
    }
    if (!pinServidor.empty() && campo(cookies, "pin", ';') != pinServidor) {
        enviar(c, "200 OK", "", paginaCodigo(false));
        return;
    }
    if (caminho == "/nome" && metodo == "POST") {
        string nome = trim(campo(corpo, "nome", '&')).substr(0, 40);
        for (size_t i = 0; i < nome.size(); i++) if (nome[i] == ';' || nome[i] == '|') nome[i] = ',';
        redirecionar(c, volta + "&m=nome", "Set-Cookie: mot=" + urlEncode(nome) + umAno);
        return;
    }
    if ((caminho == "/baixa" || caminho == "/desfazer") && metodo == "POST") {
        string m = "erro";
        if (caminho == "/baixa" && trim(motorista).empty()) m = "semnome";
        else {
            Trava t;
            int idx = indiceServico(atoi(campo(corpo, "id", '&').c_str()));
            if (idx >= 0 && caminho == "/baixa" && servicos[idx].status == "PENDENTE") {
                servicos[idx].status = "FEITO";
                servicos[idx].motorista = motorista;
                salvarServicos();
                m = "ok";
                cout << "\n  [celular] " << motorista << " deu baixa: "
                     << (servicos[idx].tipo == 'E' ? "entrega" : "retirada") << " de "
                     << nomeCliente(servicos[idx].clienteId) << " (ID " << servicos[idx].id << ")\n";
            } else if (idx >= 0 && caminho == "/desfazer" && servicos[idx].status == "FEITO") {
                servicos[idx].status = "PENDENTE";
                servicos[idx].motorista = "";
                salvarServicos();
                m = "desfeito";
            } else m = "ja";
        }
        redirecionar(c, volta + "&m=" + m, "");
        return;
    }
    if (caminho == "/" && metodo == "GET") {
        string m = campo(query, "m", '&'), msg;
        bool erro = false;
        if (m == "ok") msg = "Baixa registrada.";
        else if (m == "desfeito") msg = "Baixa desfeita.";
        else if (m == "nome") msg = "Nome salvo neste celular.";
        else if (m == "semnome") { msg = "Escreva seu nome no campo Motorista antes de dar baixa."; erro = true; }
        else if (m == "ja") { msg = "Essa parada j&aacute; tinha sido atualizada por outra pessoa."; erro = true; }
        string pagina;
        {
            Trava t;
            pagina = htmlPainel(dia, true, motorista, msg, erro);
        }
        enviar(c, "200 OK", "", pagina);
        return;
    }
    enviar(c, "404 Not Found", "", "<!doctype html><meta charset=\"utf-8\"><p>P&aacute;gina n&atilde;o encontrada. <a href=\"/\">Voltar</a></p>");
}

void lacoServidor() {
    while (servidorLigado) {
        Sock c = accept(socketServidor, NULL, NULL);
        if (c == INVALID_SOCKET) { if (!servidorLigado) break; continue; }
        atender(c);
        FECHAR(c);
    }
}

#ifdef _WIN32
DWORD WINAPI lacoServidorWin(LPVOID) { lacoServidor(); return 0; }
#else
void* lacoServidorPosix(void*) { lacoServidor(); return NULL; }
#endif

/* Descobre o IP do PC na rede local (nao envia nada pela rede). */
string ipLocal() {
    Sock s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == INVALID_SOCKET) return "";
    sockaddr_in destino;
    memset(&destino, 0, sizeof destino);
    destino.sin_family = AF_INET;
    destino.sin_port = htons(80);
    destino.sin_addr.s_addr = inet_addr("8.8.8.8");
    string ip;
    if (connect(s, (sockaddr*)&destino, sizeof destino) == 0) {
        sockaddr_in local;
        socklen_t tam = sizeof local;
        if (getsockname(s, (sockaddr*)&local, &tam) == 0) ip = inet_ntoa(local.sin_addr);
    }
    FECHAR(s);
    return ip;
}

void ligarServidor() {
    cout << "\n=== SERVIDOR PARA OS CELULARES ===\n";
    portaServidor = lerInteiroPadrao("Porta (Enter = 8080): ", 1024, 65535, 8080);
    pinServidor = lerLinha("Codigo de acesso para os motoristas (Enter = sem codigo): ");
    Sock s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) { cout << "  ! Nao foi possivel criar o servidor.\n"; return; }
    int sim = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&sim, sizeof sim);
    sockaddr_in end;
    memset(&end, 0, sizeof end);
    end.sin_family = AF_INET;
    end.sin_addr.s_addr = htonl(INADDR_ANY);
    end.sin_port = htons((unsigned short)portaServidor);
    if (bind(s, (sockaddr*)&end, sizeof end) != 0 || listen(s, 8) != 0) {
        cout << "  ! A porta " << portaServidor << " esta em uso. Tente outra.\n";
        FECHAR(s);
        return;
    }
    socketServidor = s;
    servidorLigado = true;
#ifdef _WIN32
    HANDLE th = CreateThread(NULL, 0, lacoServidorWin, NULL, 0, NULL);
    if (th) CloseHandle(th);
#else
    pthread_t th;
    pthread_create(&th, NULL, lacoServidorPosix, NULL);
    pthread_detach(th);
#endif
    string ip = ipLocal();
    enderecoServidor = "http://" + (ip.empty() ? string("IP-DO-PC") : ip) + ":" + to_string(portaServidor);
    cout << "Servidor ligado. Nos celulares (mesmo Wi-Fi), abra:\n\n    " << enderecoServidor << "\n\n"
         << "Neste PC: http://localhost:" << portaServidor << "\n"
         << (pinServidor.empty() ? "Sem codigo de acesso: qualquer pessoa no Wi-Fi consegue dar baixa.\n"
                                 : "Os motoristas vao digitar o codigo uma vez em cada celular.\n")
         << "Se o Windows perguntar sobre o firewall, permita o acesso em redes privadas.\n"
         << "O menu continua funcionando normalmente enquanto o servidor estiver ligado.\n";
}

void desligarServidor() {
    servidorLigado = false;
    if (socketServidor != INVALID_SOCKET) {
#ifdef _WIN32
        closesocket(socketServidor);
#else
        shutdown(socketServidor, SHUT_RDWR);
        close(socketServidor);
#endif
        socketServidor = INVALID_SOCKET;
    }
    cout << "Servidor desligado.\n";
}

/* ======================================================================
 *                                 MAIN
 * ====================================================================== */
int main() {
    iniciarTrava();
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    carregarClientes();
    carregarItens();
    carregarServicos();
    gerarPainelArquivo();

    while (true) {
        int pendHoje = 0;
        for (size_t i = 0; i < servicos.size(); i++)
            if (servicos[i].data == hoje() && servicos[i].status == "PENDENTE") pendHoje++;
        int alertas = contarAlertas();

        cout << "\n==============================================\n"
                "  CONTROLE DE ENTREGAS, RETIRADAS E ESTOQUE\n"
                "  Hoje: " << diaSemana(hoje()) << " " << hoje()
             << "  |  Pendentes hoje: " << pendHoje << "\n";
        if (alertas)
            cout << "  !! " << alertas << " item(ns) com estoque baixo ou em falta nos\n"
                    "     proximos 7 dias (veja a opcao 6)\n";
        if (servidorLigado)
            cout << "  Celulares: " << enderecoServidor << "\n";
        cout << "==============================================\n"
                "1 - Clientes\n"
                "2 - Novo pedido (entrega + retirada)\n"
                "3 - Programacao do dia (roteiro)\n"
                "4 - Dar baixa / alterar / cancelar\n"
                "5 - Consultas e relatorios\n"
                "6 - Itens e estoque\n"
                "7 - Abrir painel no navegador\n"
             << (servidorLigado ? "8 - Desligar servidor dos celulares\n" : "8 - Ligar servidor para os celulares\n")
             << "0 - Sair\n";
        int op = lerInteiro("Opcao: ", 0, 8);
        switch (op) {
            case 1: menuClientes(); break;
            case 2: novoPedido(); break;
            case 3: programacaoDoDia(); break;
            case 4: menuAgendamentos(); break;
            case 5: menuRelatorios(); break;
            case 6: menuItens(); break;
            case 7: abrirPainel(); break;
            case 8: if (servidorLigado) desligarServidor(); else ligarServidor(); break;
            case 0:
                if (servidorLigado) desligarServidor();
                cout << "Ate logo!\n";
                return 0;
        }
    }
}
