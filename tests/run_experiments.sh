#!/bin/bash

# Verificar se os binários existem
if [ ! -f "bin/controller" ] || [ ! -f "bin/runner" ]; then
    echo "Aviso: Binários em 'bin/' não encontrados. Certifica-te que corres 'make' primeiro."
    exit 1
fi

LOG_FILE="tmp/log.txt"

# Função auxiliar para correr uma experiência
# Argumentos: 
# 1: Nome do teste
# 2: Max slots
# 3: Policy
run_experiment_fairness() {
    local name="$1"
    local max_slots="$2"
    local policy="$3"

    echo ""
    echo "=================================================="
    echo "Iniciando Experiência: $name"
    echo "Configuração: $max_slots slots, Política: $policy"
    echo "--------------------------------------------------"

    # Limpar ficheiro de log
    > "$LOG_FILE"

    # Iniciar controller em background
    ./bin/controller "$max_slots" "$policy" > /dev/null 2>&1 &
    CONTROLLER_PID=$!
    sleep 0.5 # dar tempo ao controller para abrir o fifo

    START_TIME=$(date +%s.%N)

    # Submeter jobs (5 para user 1, 5 para user 2)
    for i in {1..5}; do
        ./bin/runner -e 1 sleep 1 > /dev/null 2>&1 &
    done

    # Atraso ligeiro
    sleep 0.1

    for i in {1..5}; do
        ./bin/runner -e 2 sleep 1 > /dev/null 2>&1 &
    done

    # Esperar que todos os runners submetidos terminem
    wait

    END_TIME=$(date +%s.%N)
    MAKESPAN=$(echo "$END_TIME - $START_TIME" | bc -l)

    # Desligar controller
    ./bin/runner -s > /dev/null 2>&1
    wait $CONTROLLER_PID

    # Analisar o log com awk
    if [ ! -s "$LOG_FILE" ]; then
        echo "Nenhum resultado obtido no log."
    else
        awk -F' ' '
        {
            split($1, uarr, "="); user = uarr[2];
            split($3, darr, "="); dur_str = darr[2];
            gsub(/s$/, "", dur_str); dur = dur_str + 0.0;
            
            count[user]++;
            total[user] += dur;
        }
        END {
            printf "%-10s | %-20s | %-15s\n", "User ID", "Média Duração (s)", "Total Comandos"
            print "--------------------------------------------------"
            # Sort output by user ID (simple approach for bash awk)
            for (u in count) {
                printf "%-10s | %-20.3f | %-15d\n", u, total[u]/count[u], count[u]
            }
        }' "$LOG_FILE" | sort -n
    fi

    printf "Tempo total do lote (makespan): %.3fs\n" "$MAKESPAN"
}

run_experiment_parallel() {
    local name="$1"
    local max_slots="$2"
    local policy="$3"

    echo ""
    echo "=================================================="
    echo "Iniciando Experiência: $name"
    echo "Configuração: $max_slots slots, Política: $policy"
    echo "--------------------------------------------------"

    > "$LOG_FILE"

    ./bin/controller "$max_slots" "$policy" > /dev/null 2>&1 &
    CONTROLLER_PID=$!
    sleep 0.5

    START_TIME=$(date +%s.%N)

    # Submeter 8 jobs independentes pelo user 1
    for i in {1..8}; do
        ./bin/runner -e 1 sleep 1 > /dev/null 2>&1 &
    done

    wait

    END_TIME=$(date +%s.%N)
    MAKESPAN=$(echo "$END_TIME - $START_TIME" | bc -l)

    ./bin/runner -s > /dev/null 2>&1
    wait $CONTROLLER_PID

    if [ ! -s "$LOG_FILE" ]; then
        echo "Nenhum resultado obtido no log."
    else
        awk -F' ' '
        {
            split($1, uarr, "="); user = uarr[2];
            split($3, darr, "="); dur_str = darr[2];
            gsub(/s$/, "", dur_str); dur = dur_str + 0.0;
            
            count[user]++;
            total[user] += dur;
        }
        END {
            printf "%-10s | %-20s | %-15s\n", "User ID", "Média Duração (s)", "Total Comandos"
            print "--------------------------------------------------"
            for (u in count) {
                printf "%-10s | %-20.3f | %-15d\n", u, total[u]/count[u], count[u]
            }
        }' "$LOG_FILE" | sort -n
    fi

    printf "Tempo total do lote (makespan): %.3fs\n" "$MAKESPAN"
}


echo ">>> 1. TESTE DE JUSTIÇA (FAIRNESS) - FIFO vs RR <<<"
run_experiment_fairness "FIFO Fairness" 1 "fifo"
run_experiment_fairness "Round-Robin (RR) Fairness" 1 "rr"

echo ""
echo ">>> 2. TESTE DE PARALELISMO <<<"
run_experiment_parallel "Paralelismo (N=1)" 1 "rr"
run_experiment_parallel "Paralelismo (N=2)" 2 "rr"
run_experiment_parallel "Paralelismo (N=4)" 4 "rr"

echo ""
echo "Testes concluídos!"
