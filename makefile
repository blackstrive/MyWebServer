CXX ?= g++

DEBUG ?= 1
ifeq ($(DEBUG), 1)
    CXXFLAGS += -g
else
    CXXFLAGS += -O2

endif

server: main.cpp ./CGImysql/sql_connection_pool.cpp ./http/http_conn.cpp ./webserver/webserver.cpp ./log/log.cpp ./timer/lst_timer.cpp
	$(CXX) -o server $^ $(CXXFLAGS) -lpthread -lmysqlclient


clean:
	rm -r server
	