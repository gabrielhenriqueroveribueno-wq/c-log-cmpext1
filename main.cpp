/*
 * ==========================================================================
 *  CONTROLE DE ENTREGAS E RETIRADAS - LOCACAO DE MESAS E CADEIRAS
 * --------------------------------------------------------------------------
 *  CMPEXT1 - Projeto de Extensao 1 | TADS-EAD IFSP
 *  Grupo .........: ____________________
 *  Integrantes ...: ____________________
 *  Demandante ....: ____________________ (locacao de mesas e cadeiras)
 *  Finalidade ....: esboco minimamente funcional. Substitui a lousa do
 *                   deposito e a planilha impressa de rotas: agenda entregas
 *                   e retiradas, monta a programacao do dia por rota
 *                   (A1, A2, B1...), registra a baixa de cada parada e mostra
 *                   o que ainda esta pendente.
 *  Tecnologia ....: C++ estruturado (funcoes + structs), menu de console,
 *                   persistencia em arquivos CSV (separador ';').
 *  Versao ........: 1.0
 * ==========================================================================
 */
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

using namespace std;

/* ------------------------------ ARQUIVOS ------------------------------ */
const string ARQ_CLIENTES = "clientes.csv";
const string ARQ_SERVICOS = "servicos.csv";
const string ARQ_CONFIG   = "config.csv";

/* ------------------------------ REGISTROS ----------------------------- */
struct Cliente {
    int id;
    string nome;
    string telefone;
    string endereco;
    string bairro;
};

struct Servico {            // uma entrega OU uma retirada
    int id;
    int clienteId;
    char tipo;              // 'E' = entrega, 'R' = retirada
    string data;            // DD/MM/AAAA
    string horario;         // HH:MM (pode ficar vazio)
    string rota;            // A1, A2, B1... (letra = viagem, numero = parada)
    int mesas;
    int cadeiras;
    string outros;          // outros itens / o que sera feito
    string endereco;        // local do servico (padrao: endereco do cliente)
    string bairro;          // "DEPOSITO" quando o cliente busca/devolve
    string status;          // PENDENTE, FEITO, CANCELADO
    string motorista;       // quem deu a baixa
    string obs;
    int vinculo;            // id da entrega/retirada par (0 = sem par)
};

vector<Cliente> clientes;
vector<Servico> servicos;
int estoqueMesas = 0;
int estoqueCadeiras = 0;

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

/* Le uma linha. O ';' e trocado por ',' para nao quebrar o CSV. */
string lerLinha(const string& prompt) {
    cout << prompt;
    string s;
    if (!getline(cin, s)) {
        cout << "\n[Entrada encerrada. Os dados ja estao salvos.]\n";
        exit(0);
    }
    s = trim(s);
    for (size_t i = 0; i < s.size(); i++) if (s[i] == ';') s[i] = ',';
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

void pausar() { lerLinha("\nPressione Enter para continuar..."); }

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

string lerHorario(const string& prompt) {
    while (true) {
        string h = normalizarHorario(lerLinha(prompt));
        if (h != "?") return h;
        cout << "  ! Horario invalido. Exemplos: 8, 8h30, 14:00.\n";
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
 *                         PERSISTENCIA EM ARQUIVO
 * ====================================================================== */
void salvarClientes() {
    ofstream f(ARQ_CLIENTES.c_str());
    f << "id;nome;telefone;endereco;bairro\n";
    for (size_t i = 0; i < clientes.size(); i++) {
        const Cliente& c = clientes[i];
        f << c.id << ';' << c.nome << ';' << c.telefone << ';'
          << c.endereco << ';' << c.bairro << '\n';
    }
}

void salvarServicos() {
    ofstream f(ARQ_SERVICOS.c_str());
    f << "id;cliente_id;tipo;data;horario;rota;mesas;cadeiras;outros;"
         "endereco;bairro;status;motorista;obs;vinculo\n";
    for (size_t i = 0; i < servicos.size(); i++) {
        const Servico& s = servicos[i];
        f << s.id << ';' << s.clienteId << ';' << s.tipo << ';' << s.data << ';'
          << s.horario << ';' << s.rota << ';' << s.mesas << ';' << s.cadeiras << ';'
          << s.outros << ';' << s.endereco << ';' << s.bairro << ';' << s.status << ';'
          << s.motorista << ';' << s.obs << ';' << s.vinculo << '\n';
    }
}

void salvarConfig() {
    ofstream f(ARQ_CONFIG.c_str());
    f << "chave;valor\n";
    f << "ESTOQUE_MESAS;" << estoqueMesas << '\n';
    f << "ESTOQUE_CADEIRAS;" << estoqueCadeiras << '\n';
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

void carregarServicos() {
    ifstream f(ARQ_SERVICOS.c_str());
    string linha;
    getline(f, linha);
    while (getline(f, linha)) {
        if (trim(linha).empty()) continue;
        vector<string> p = dividir(trim(linha), ';');
        if (p.size() != 15 || !ehNumero(p[0]) || !ehNumero(p[1]) ||
            !dataValida(p[3]) || !ehNumero(p[6]) || !ehNumero(p[7]) ||
            !ehNumero(p[14]) || p[2].size() != 1) {
            cout << "  ! Linha ignorada em " << ARQ_SERVICOS << ": " << linha << '\n';
            continue;
        }
        Servico s;
        s.id = atoi(p[0].c_str()); s.clienteId = atoi(p[1].c_str());
        s.tipo = p[2][0]; s.data = p[3]; s.horario = p[4]; s.rota = p[5];
        s.mesas = atoi(p[6].c_str()); s.cadeiras = atoi(p[7].c_str());
        s.outros = p[8]; s.endereco = p[9]; s.bairro = p[10]; s.status = p[11];
        s.motorista = p[12]; s.obs = p[13]; s.vinculo = atoi(p[14].c_str());
        servicos.push_back(s);
    }
}

void carregarConfig() {
    ifstream f(ARQ_CONFIG.c_str());
    string linha;
    getline(f, linha);
    while (getline(f, linha)) {
        vector<string> p = dividir(trim(linha), ';');
        if (p.size() != 2 || !ehNumero(p[1])) continue;
        if (p[0] == "ESTOQUE_MESAS") estoqueMesas = atoi(p[1].c_str());
        if (p[0] == "ESTOQUE_CADEIRAS") estoqueCadeiras = atoi(p[1].c_str());
    }
}

/* ======================================================================
 *                              BUSCAS
 * ====================================================================== */
int indiceCliente(int id) {
    for (size_t i = 0; i < clientes.size(); i++) if (clientes[i].id == id) return (int)i;
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

int proximoIdCliente() {
    int m = 0;
    for (size_t i = 0; i < clientes.size(); i++) m = max(m, clientes[i].id);
    return m + 1;
}

int proximoIdServico() {
    int m = 0;
    for (size_t i = 0; i < servicos.size(); i++) m = max(m, servicos[i].id);
    return m + 1;
}

/* Ordem da programacao: rota (A1, A2, B1...), depois horario, depois id. */
bool vemAntes(const Servico& a, const Servico& b) {
    if (chaveRota(a.rota) != chaveRota(b.rota)) return chaveRota(a.rota) < chaveRota(b.rota);
    string ha = a.horario.empty() ? "99:99" : a.horario;
    string hb = b.horario.empty() ? "99:99" : b.horario;
    if (ha != hb) return ha < hb;
    return a.id < b.id;
}

/* Ordem dos relatorios: data, depois a ordem da programacao. */
bool vemAntesComData(const Servico& a, const Servico& b) {
    if (chaveData(a.data) != chaveData(b.data)) return chaveData(a.data) < chaveData(b.data);
    return vemAntes(a, b);
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
         col("CLIENTE", 20) + col("BAIRRO", 14) + col("MES", 3) + col("CAD", 3) + "OUTROS";
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
      << col(to_string(s.mesas), 3) << col(to_string(s.cadeiras), 3) << s.outros << '\n';
    if (detalhes) {
        int ic = indiceCliente(s.clienteId);
        string extra;
        if (!s.endereco.empty()) extra += "End.: " + s.endereco;
        if (ic >= 0 && !clientes[ic].telefone.empty())
            extra += (extra.empty() ? "" : " | ") + string("Tel: ") + clientes[ic].telefone;
        if (!s.obs.empty()) extra += (extra.empty() ? "" : " | ") + string("Obs: ") + s.obs;
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
    clientes.push_back(c);
    salvarClientes();
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
    Cliente& c = clientes[i];
    cout << "Enter mantem o valor atual.\n";
    string v;
    v = lerLinha("Nome [" + c.nome + "]: ");          if (!v.empty()) c.nome = v;
    v = lerLinha("Telefone [" + c.telefone + "]: ");  if (!v.empty()) c.telefone = v;
    v = lerLinha("Endereco [" + c.endereco + "]: ");  if (!v.empty()) c.endereco = v;
    v = lerLinha("Bairro [" + c.bairro + "]: ");      if (!v.empty()) c.bairro = maiusculas(v);
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
 *                       AGENDAMENTO (RF04-RF06)
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

/* Agenda a retirada ligada a uma entrega (mesmos itens e local). */
void agendarRetiradaVinculada(int idEntrega) {
    int ie = indiceServico(idEntrega);
    if (ie < 0) return;
    Servico r = servicos[ie];                       // copia itens e local
    cout << "\n--- Retirada de " << nomeCliente(r.clienteId)
         << " (entrega em " << r.data << ") ---\n";
    while (true) {
        r.data = lerData("Data da retirada", false);
        if (chaveData(r.data) >= chaveData(servicos[ie].data)) break;
        cout << "  ! A retirada nao pode ser antes da entrega.\n";
    }
    r.id = proximoIdServico();
    r.tipo = 'R';
    r.horario = lerHorario("Horario (Enter = sem horario): ");
    r.rota = lerRota("Rota (ex.: A1, Enter = definir depois): ", r.data, 0, "");
    r.status = "PENDENTE";
    r.motorista = "";
    r.obs = lerTexto("Observacao (opcional): ", false);
    r.vinculo = idEntrega;
    servicos.push_back(r);
    servicos[indiceServico(idEntrega)].vinculo = r.id;  // indice pode ter mudado
    salvarServicos();
    cout << "Retirada agendada com ID " << r.id << ".\n";
}

void agendarServico() {
    cout << "\n=== AGENDAR ENTREGA / RETIRADA ===\n";
    int cid = selecionarCliente();
    if (cid == 0) return;
    const Cliente cli = clientes[indiceCliente(cid)];

    Servico s;
    s.id = proximoIdServico();
    s.clienteId = cid;
    while (true) {
        string t = maiusculas(lerLinha("Tipo (E = entrega, R = retirada): "));
        if (t == "E" || t == "R") { s.tipo = t[0]; break; }
        cout << "  ! Digite E ou R.\n";
    }
    s.data = lerData("Data", false);
    s.horario = lerHorario("Horario (Enter = sem horario): ");

    while (true) {
        s.mesas = lerInteiroPadrao("Quantidade de mesas (Enter = 0): ", 0, 9999, 0);
        s.cadeiras = lerInteiroPadrao("Quantidade de cadeiras (Enter = 0): ", 0, 99999, 0);
        s.outros = lerTexto("Outros itens / o que sera feito (opcional): ", false);
        if (s.mesas + s.cadeiras > 0 || !s.outros.empty()) break;
        cout << "  ! Informe mesas, cadeiras ou descreva outro item.\n";
    }
    if (s.tipo == 'E' && ((estoqueMesas > 0 && s.mesas > estoqueMesas) ||
                          (estoqueCadeiras > 0 && s.cadeiras > estoqueCadeiras)))
        cout << "  ! Atencao: quantidade maior que o estoque total cadastrado ("
             << estoqueMesas << " mesas / " << estoqueCadeiras << " cadeiras).\n";

    cout << "Local (digite DEPOSITO se o cliente busca/devolve no deposito).\n";
    string b = lerLinha("Bairro [Enter = " + cli.bairro + "]: ");
    s.bairro = b.empty() ? cli.bairro : maiusculas(b);
    if (s.bairro == "DEPOSITO") s.endereco = "";
    else {
        string e = lerLinha("Endereco [Enter = " + (cli.endereco.empty() ? string("nao informado") : cli.endereco) + "]: ");
        s.endereco = e.empty() ? cli.endereco : e;
    }
    s.rota = lerRota("Rota (ex.: A1, Enter = definir depois): ", s.data, 0, "");
    s.obs = lerTexto("Observacao (opcional): ", false);
    s.status = "PENDENTE";
    s.motorista = "";
    s.vinculo = 0;

    servicos.push_back(s);
    salvarServicos();
    cout << (s.tipo == 'E' ? "Entrega" : "Retirada") << " agendada com ID " << s.id << ".\n";

    if (s.tipo == 'E' && lerSimNao("Agendar a retirada desta entrega agora?"))
        agendarRetiradaVinculada(s.id);
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
    int mesasSai = 0, cadSai = 0, mesasVolta = 0, cadVolta = 0;
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
        if (s.tipo == 'E') {
            ent++; if (s.status == "PENDENTE") entPend++;
            mesasSai += s.mesas; cadSai += s.cadeiras;
        } else {
            ret++; if (s.status == "PENDENTE") retPend++;
            mesasVolta += s.mesas; cadVolta += s.cadeiras;
        }
    }
    o << string(89, '-') << '\n'
      << "Entregas: " << ent << " (pendentes: " << entPend << ")  -> saem "
      << mesasSai << " mesas e " << cadSai << " cadeiras\n"
      << "Retiradas: " << ret << " (pendentes: " << retPend << ") -> voltam "
      << mesasVolta << " mesas e " << cadVolta << " cadeiras\n"
      << "[ ] pendente   [X] feito\n";
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
        if (idx < 0 || servicos[idx].status != "PENDENTE") {
            cout << "  ! ID nao encontrado ou ja concluido.\n";
            continue;
        }
        Servico& s = servicos[idx];
        if (s.tipo == 'R' && s.vinculo != 0) {
            int iv = indiceServico(s.vinculo);
            if (iv >= 0 && servicos[iv].status == "PENDENTE")
                cout << "  ! Atencao: a entrega ligada a esta retirada ainda esta pendente.\n";
        }
        string m = lerLinha("Motorista" + (ultimoMotorista.empty() ? string("") :
                            " [Enter = " + ultimoMotorista + "]") + ": ");
        if (m.empty()) m = ultimoMotorista;
        string obs = lerTexto("Observacao da baixa (opcional): ", false);
        if (!lerSimNao("Confirmar baixa de " + nomeCliente(s.clienteId) + "?")) continue;
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
    cout << "Enter mantem o valor atual.\n";
    string d = lerLinha("Nova data [" + servicos[idx].data + "]: ");
    string novaData = servicos[idx].data;
    if (!d.empty()) {
        novaData = normalizarData(d);
        if (novaData.empty()) { cout << "  ! Data invalida. Nada foi alterado.\n"; return; }
    }
    string h = lerLinha("Novo horario [" + servicos[idx].horario + "] (- = sem horario): ");
    string novoHorario = servicos[idx].horario;
    if (h == "-") novoHorario = "";
    else if (!h.empty()) {
        novoHorario = normalizarHorario(h);
        if (novoHorario == "?") { cout << "  ! Horario invalido. Nada foi alterado.\n"; return; }
    }
    string novaRota = lerRota("Nova rota [" + servicos[idx].rota + "]: ", novaData,
                              servicos[idx].id, servicos[idx].rota);

    int iv = servicos[idx].vinculo ? indiceServico(servicos[idx].vinculo) : -1;
    if (iv >= 0 && servicos[iv].status != "CANCELADO") {
        bool ehEntrega = servicos[idx].tipo == 'E';
        int dEnt = ehEntrega ? chaveData(novaData) : chaveData(servicos[iv].data);
        int dRet = ehEntrega ? chaveData(servicos[iv].data) : chaveData(novaData);
        if (dRet < dEnt) {
            cout << "  ! A retirada ficaria antes da entrega. Nada foi alterado.\n";
            return;
        }
    }
    servicos[idx].data = novaData;
    servicos[idx].horario = novoHorario;
    servicos[idx].rota = novaRota;
    salvarServicos();
    cout << "Agendamento atualizado.\n";
}

void cancelarServico() {
    int idx = pedirServico("\nID do agendamento (0 = voltar): ");
    if (idx < 0) return;
    if (servicos[idx].status == "CANCELADO") { cout << "  ! Ja esta cancelado.\n"; return; }
    if (!lerSimNao("Cancelar este agendamento?")) return;
    servicos[idx].status = "CANCELADO";
    int iv = servicos[idx].vinculo ? indiceServico(servicos[idx].vinculo) : -1;
    if (iv >= 0 && servicos[iv].status == "PENDENTE" &&
        lerSimNao(string("Cancelar tambem a ") + (servicos[iv].tipo == 'E' ? "entrega" : "retirada") +
                  " ligada (ID " + to_string(servicos[iv].id) + ")?"))
        servicos[iv].status = "CANCELADO";
    salvarServicos();
    cout << "Cancelado.\n";
}

void menuAgendamentos() {
    while (true) {
        cout << "\n----- BAIXA E ALTERACOES -----\n"
                "1 - Dar baixa (marcar como feito)\n"
                "2 - Desfazer baixa\n"
                "3 - Alterar data, horario ou rota\n"
                "4 - Cancelar agendamento\n"
                "0 - Voltar\n";
        int op = lerInteiro("Opcao: ", 0, 4);
        if (op == 0) return;
        if (op == 1) darBaixa();
        if (op == 2) desfazerBaixa();
        if (op == 3) reagendar();
        if (op == 4) cancelarServico();
    }
}

/* ======================================================================
 *                         CONSULTAS E RELATORIOS (RF12-RF16)
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
         << col("DIA", 8) << col("DATA", 10) << col("ENTREG.", 7) << col("RETIR.", 7)
         << col("MESAS SAEM", 10) << col("CAD. SAEM", 10) << "MESAS/CAD. VOLTAM\n"
         << string(78, '-') << '\n';
    for (int d = 0; d < 7; d++) {
        string data = somarDias(inicio, d);
        int e = 0, r = 0, ms = 0, cs = 0, mv = 0, cv = 0;
        for (size_t i = 0; i < servicos.size(); i++) {
            const Servico& s = servicos[i];
            if (s.data != data || s.status == "CANCELADO") continue;
            if (s.tipo == 'E') { e++; ms += s.mesas; cs += s.cadeiras; }
            else { r++; mv += s.mesas; cv += s.cadeiras; }
        }
        cout << col(diaSemana(data), 8) << col(data, 10) << col(to_string(e), 7)
             << col(to_string(r), 7) << col(to_string(ms), 10) << col(to_string(cs), 10)
             << mv << " / " << cv << '\n';
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

void relatorioForaDoDeposito() {
    int m = 0, c = 0;
    for (size_t i = 0; i < servicos.size(); i++) {
        const Servico& s = servicos[i];
        if (s.status != "FEITO") continue;
        int sinal = s.tipo == 'E' ? 1 : -1;
        m += sinal * s.mesas;
        c += sinal * s.cadeiras;
    }
    cout << "\n=== EQUIPAMENTO FORA DO DEPOSITO (pelas baixas registradas) ===\n"
         << "Mesas na rua ....: " << m << '\n'
         << "Cadeiras na rua .: " << c << '\n';
    if (estoqueMesas > 0 || estoqueCadeiras > 0)
        cout << "No deposito .....: " << estoqueMesas - m << " mesas e "
             << estoqueCadeiras - c << " cadeiras (estoque total: "
             << estoqueMesas << " / " << estoqueCadeiras << ")\n";
    else
        cout << "(Cadastre o estoque total no menu 6 para ver o saldo no deposito.)\n";
    if (m < 0 || c < 0)
        cout << "  ! Valor negativo: ha retiradas com baixa sem a entrega correspondente.\n";
}

void relatorioSemRetirada() {
    vector<Servico> l;
    for (size_t i = 0; i < servicos.size(); i++) {
        const Servico& s = servicos[i];
        if (s.tipo != 'E' || s.status == "CANCELADO") continue;
        int iv = s.vinculo ? indiceServico(s.vinculo) : -1;
        if (iv < 0 || servicos[iv].status == "CANCELADO") l.push_back(s);
    }
    cout << "\n=== ENTREGAS SEM RETIRADA AGENDADA ===\n";
    listarFiltrados(l, "Todas as entregas ja tem retirada agendada.");
    if (l.empty()) return;
    int id = lerInteiro("ID da entrega para agendar a retirada (0 = voltar): ", 0, 999999999);
    if (id == 0) return;
    for (size_t i = 0; i < l.size(); i++)
        if (l[i].id == id) { agendarRetiradaVinculada(id); return; }
    cout << "  ! ID nao esta na lista.\n";
}

void menuRelatorios() {
    while (true) {
        cout << "\n----- CONSULTAS E RELATORIOS -----\n"
                "1 - Pendencias de dias anteriores\n"
                "2 - Resumo dos proximos 7 dias\n"
                "3 - Historico de um cliente\n"
                "4 - Mesas e cadeiras fora do deposito\n"
                "5 - Entregas sem retirada agendada\n"
                "0 - Voltar\n";
        int op = lerInteiro("Opcao: ", 0, 5);
        if (op == 0) return;
        if (op == 1) relatorioAtrasados();
        if (op == 2) relatorioProximosDias();
        if (op == 3) relatorioCliente();
        if (op == 4) relatorioForaDoDeposito();
        if (op == 5) relatorioSemRetirada();
    }
}

void configurarEstoque() {
    cout << "\n=== ESTOQUE TOTAL DA EMPRESA ===\n";
    estoqueMesas = lerInteiroPadrao("Total de mesas [" + to_string(estoqueMesas) + "]: ",
                                    0, 99999, estoqueMesas);
    estoqueCadeiras = lerInteiroPadrao("Total de cadeiras [" + to_string(estoqueCadeiras) + "]: ",
                                       0, 999999, estoqueCadeiras);
    salvarConfig();
    cout << "Estoque salvo.\n";
}

/* ======================================================================
 *                                 MAIN
 * ====================================================================== */
int main() {
    carregarClientes();
    carregarServicos();
    carregarConfig();

    while (true) {
        int pendHoje = 0;
        for (size_t i = 0; i < servicos.size(); i++)
            if (servicos[i].data == hoje() && servicos[i].status == "PENDENTE") pendHoje++;

        cout << "\n==============================================\n"
                "  CONTROLE DE ENTREGAS E RETIRADAS\n"
                "  Hoje: " << diaSemana(hoje()) << " " << hoje()
             << "  |  Pendentes hoje: " << pendHoje << "\n"
                "==============================================\n"
                "1 - Clientes\n"
                "2 - Agendar entrega / retirada\n"
                "3 - Programacao do dia (roteiro)\n"
                "4 - Dar baixa / alterar / cancelar\n"
                "5 - Consultas e relatorios\n"
                "6 - Configurar estoque total\n"
                "0 - Sair\n";
        int op = lerInteiro("Opcao: ", 0, 6);
        switch (op) {
            case 1: menuClientes(); break;
            case 2: agendarServico(); break;
            case 3: programacaoDoDia(); break;
            case 4: menuAgendamentos(); break;
            case 5: menuRelatorios(); break;
            case 6: configurarEstoque(); break;
            case 0: cout << "Ate logo!\n"; return 0;
        }
    }
}
