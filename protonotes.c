#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sqlite3.h>

#define DB_FILE "protonotes.db"
#define NOTES_DIR "notes_storage"

// garantir a existência do diretório de notas
void ensure_notes_directory(){
#if defined(_WIN32)
    _mkdir(NOTES_DIR);
#else
    mkdir(NOTES_DIR, 0755);
#endif
}

// criar o banco (caso não exista) e iniciar o banco 
sqlite3* init_db(){
    sqlite3 *db;
    int rc = sqlite3_open(DB_FILE, &db);
    if(rc != SQLITE_OK){
        fprintf(stderr, "erro ao abrir o banco sqlite: %s\n", sqlite3_errmsg(db));
        return NULL;
    }

    const char *sql_create_table = 
        "CREATE TABLE IF NOT EXISTS notes ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "title TEXT NOT NULL UNIQUE, "
        "file_path TEXT NOT NULL, "
        "created_at DATETIME DEFAULT CURRENT_TIMESTAMP);";

    char *err_msg = 0;
    rc = sqlite3_exec(db, sql_create_table, 0, 0, &err_msg);
    if(rc != SQLITE_OK){
        fprintf(stderr, "erro na tabela do banco: %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db);
        return NULL;
    }

    return db;
}

// CRUD
void create_note(sqlite3 *db, const char *title, const char *content){
    ensure_notes_directory();

    char file_path[256];
    snprintf(file_path, sizeof(file_path), "%s/%s.md", NOTES_DIR, title);

    FILE *f = fopen(file_path, "w");
    if(!f){
        perror("erro ao criar arquivo .md");
        return;
    }
    fputs(content, f);
    fclose(f);

    const char *sql = "INSERT INTO notes (title, file_path) VALUES (?, ?);";
    sqlite3_stmt *stmt;

    if(sqlite3_prepare_v2(db, sql, -1, &stmt, 0) == SQLITE_OK){
        sqlite3_bind_text(stmt, 1, title, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, file_path, -1, SQLITE_STATIC);

        if(sqlite3_step(stmt) == SQLITE_DONE){
            long long id = sqlite3_last_insert_rowid(db);
            printf("nota %s criada com sucesso! ID: %lld, arquivo: %s\n\n", title, id, file_path);
        }else{
            fprintf(stderr, "erro (%s) ao salvar nota no banco\n\n", sqlite3_errmsg(db));
        }
    }
    sqlite3_finalize(stmt);
}

void read_note(sqlite3 *db, int id) {
    const char *sql = "SELECT title, file_path FROM notes WHERE id = ?;";
    sqlite3_stmt *stmt;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, id);

        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const char *title = (const char*)sqlite3_column_text(stmt, 0);
            const char *file_path = (const char*)sqlite3_column_text(stmt, 1);

            printf("--- ID: %d | Titulo: %s ---\n", id, title);

            FILE *f = fopen(file_path, "r");
            if (!f) {
                perror("Erro ao abrir arquivo .md da nota");
            } else {
                char buffer[1024];
                while (fgets(buffer, sizeof(buffer), f)) {
                    printf("%s", buffer);
                }
                printf("\n\n");
                fclose(f);
            }
        } else {
            printf("[!] Nota com ID %d nao encontrada.\n", id);
        }
    }
    sqlite3_finalize(stmt);
}

void list_notes(sqlite3 *db){
    const char *sql = "SELECT id, title, file_path, created_at FROM notes;";
    sqlite3_stmt *stmt;

    if(sqlite3_prepare_v2(db, sql, -1, &stmt, 0) == SQLITE_OK){
        while(sqlite3_step(stmt) == SQLITE_ROW){
            int id = sqlite3_column_int(stmt, 0);
            const char *title = (const char*)sqlite3_column_text(stmt, 1);
            const char *file_path = (const char*)sqlite3_column_text(stmt, 2);
            const char *created_at = (const char*)sqlite3_column_text(stmt, 3);

            printf("ID: %d, \n title: %s, \n file_path: %s, \n created_at: %s \n", id, title, file_path, created_at);
        }
    }

    sqlite3_finalize(stmt);
}

void update_note(sqlite3 *db, int id, const char *new_content){
    const char *sql = "SELECT file_path FROM notes WHERE id = ?;";
    sqlite3_stmt *stmt;

    if(sqlite3_prepare_v2(db, sql, -1, &stmt, 0) == SQLITE_OK){
        sqlite3_bind_int(stmt, 1, id);

        if(sqlite3_step(stmt) == SQLITE_ROW){
            const char *file_path = (const char*)sqlite3_column_text(stmt, 0);
            FILE *f = fopen(file_path, "w");
            if(f){
                fputs(new_content, f);
                fclose(f);

                printf("conteúdo modificado com sucesso\n");
            }else{
                perror("erro ao atualizar arquivo\n");
            }
        }else{
            printf("arquivo não encontrado\n");
        }
    }
    sqlite3_finalize(stmt);
}

void delete_note(sqlite3 *db, int id){
    // encontrar no banco o caminho do arquivo
    const char *sql = "SELECT file_path FROM notes WHERE id = ?;";
    sqlite3_stmt *stmt;
    char file_path[256] = {0};

    if(sqlite3_prepare_v2(db, sql, -1, &stmt, 0) == SQLITE_OK){
        sqlite3_bind_int(stmt, 1, id);

        if(sqlite3_step(stmt) == SQLITE_ROW){
            strncpy(file_path, (const char*)sqlite3_column_text(stmt, 0), sizeof(file_path) -1);    
        }
    }
    sqlite3_finalize(stmt);

    if(strlen(file_path) == 0){
        printf("nota não encontrada");
        return;
    }

    // deletar o indice do banco e deletar o arquivo .md
    const char *sql_delete = "DELETE FROM notes WHERE id = ?;";
    if(sqlite3_prepare_v2(db, sql_delete, -1, &stmt, 0) == SQLITE_OK){
        sqlite3_bind_int(stmt, 1, id);
        
        if(sqlite3_step(stmt) == SQLITE_DONE){
            printf("nota com id: %d removida do banco com sucesso\n", id);
            
            remove(file_path);
            printf("nota %s.md removida com sucesso\n", file_path);
        }
    }
    sqlite3_finalize(stmt);

}

// menu do app
int main(int argc, char *argv[]){
    if(argc < 2){
        printf("uso do protonotes:\n");
        printf(" %s -c <titulo> <conteudo>  : criar nota\n", argv[0]);
        printf(" %s -r <id>                 : ler nota por ID\n", argv[0]);
        printf(" %s -l                      : listar todas as notas\n", argv[0]);
        printf(" %s -u <id> <novo_conteudo> : atualizar conteudo da nota\n", argv[0]);
        printf(" %s -d <id>                 : deletar nota por ID\n", argv[0]);
        return 1;
    }

    sqlite3 *db = init_db();
    if(!db) return 1;

    char *flag = argv[1];

    if(strcmp(flag, "-c") == 0 && argc >= 4){
        create_note(db, argv[2], argv[3]);
    }else if (strcmp(flag, "-r") == 0 && argc >= 3){
        read_note(db, atoi(argv[2]));
    }else if (strcmp(flag, "-l") == 0){
        list_notes(db);
    }else if (strcmp(flag, "-u") == 0 && argc >= 4){
        update_note(db, atoi(argv[2]), argv[3]);    
    }else if (strcmp(flag, "-d") == 0 && argc >= 3){
        delete_note(db, atoi(argv[2]));
    }else{
        printf("[!] argumentos ou flag inválidos\n");
    }

    sqlite3_close(db);
    return 0;
}