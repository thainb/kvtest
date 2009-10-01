#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <iostream>

#include <sqlite3.h>

#include "base-test.hh"

#define MAX_STEPS 10000

class PreparedStatement {
public:
    PreparedStatement(sqlite3 *d, const char *query) {
        db = d;
        if(sqlite3_prepare_v2(db, query, strlen(query), &st, NULL)
           != SQLITE_OK) {
            throw std::runtime_error(sqlite3_errmsg(db));
        }
    }

    ~PreparedStatement() {
        sqlite3_finalize(st);
    }

    void bind(int pos, const char *s) {
        sqlite3_bind_text(st, pos, s, strlen(s), SQLITE_TRANSIENT);
    }

    int execute() {
        int steps_run = 0, rc = 0;
        while ((rc = sqlite3_step(st)) != SQLITE_DONE) {
            steps_run++;
            assert(steps_run < MAX_STEPS);
        }
        return sqlite3_changes(db);
    }

    bool fetch() {
        bool rv = true;
        assert(st);
        switch(sqlite3_step(st)) {
        case SQLITE_BUSY:
            throw std::runtime_error("DB was busy.");
            break;
        case SQLITE_ROW:
            break;
        case SQLITE_DONE:
            rv = false;
            break;
        default:
            throw std::runtime_error("Unhandled case.");
        }
        return rv;
    }

    const char *column(int x) {
        return (char*)sqlite3_column_text(st, x);
    }

private:
    sqlite3      *db;
    sqlite3_stmt *st;
};

class Sqlite3 : public kvtest::ThingUnderTest {
public:

    Sqlite3(const char *fn) {
        filename = fn;
        open();
    }

    ~Sqlite3() {
        close();
    }

    void open() {
        if(!db) {
            if(sqlite3_open(filename, &db) !=  SQLITE_OK) {
                throw std::runtime_error("Error initializing sqlite3");
            }

            initTables();
        }
    }

    void close() {
        if(db) {
            sqlite3_close(db);
            db = NULL;
        }
    }

    void initTables() {
        execute("create table if not exists kv"
                " (k varchar(250) primary key on conflict replace,"
                "  v text)");
    }

    void destroyTables() {
        execute("drop table if exists kv");
    }

    void reset() {
        close();
        open();
        destroyTables();
        initTables();
        execute("vacuum");
    }

    void set(std::string &key, std::string &val,
             kvtest::Callback<bool> &cb) {
        PreparedStatement st(db, "insert into kv(k,v) values(?, ?)");
        st.bind(1, key.c_str());
        st.bind(2, val.c_str());
        bool rv = st.execute() == 1;
        cb.callback(rv);
    }

    void get(std::string &key, kvtest::Callback<kvtest::GetValue> &cb) {
        PreparedStatement st(db, "select v from kv where k = ?");
        st.bind(1, key.c_str());

        if(st.fetch()) {
            std::string str(st.column(0));
            kvtest::GetValue rv(str, true);
            cb.callback(rv);
        } else {
            std::string str(":(");
            kvtest::GetValue rv(str, false);
            cb.callback(rv);
        }
    }

    void del(std::string &key, kvtest::Callback<bool> &cb) {
        PreparedStatement st(db, "delete from kv where k = ?");
        st.bind(1, key.c_str());
        bool rv = st.execute() == 1;
        cb.callback(rv);
    }

protected:
    void execute(const char *query) {
        PreparedStatement st(db, query);
        st.execute();
    }

private:
    const char *filename;
    sqlite3 *db;
};

int main(int argc, char **args) {
    Sqlite3 *thing = new Sqlite3("/tmp/test.db");

    kvtest::TestSuite suite(thing);
    return suite.run() ? 0 : 1;
}
