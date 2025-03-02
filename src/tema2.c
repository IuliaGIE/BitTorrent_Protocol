#include <mpi.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TRACKER_RANK 0
#define MAX_FILES 10
#define MAX_FILENAME 15
#define HASH_SIZE 40
#define MAX_CHUNKS 100

pthread_mutex_t mutex_args = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    char filename[MAX_FILENAME];
    int segments;
    char hashes[MAX_CHUNKS][HASH_SIZE];
} metadata;

typedef struct {
    int total_owned_files;
    metadata files[MAX_FILES];
} peer_tracker_info;

typedef struct {
    char filename[MAX_FILENAME];
    int segments;
    char hashes[MAX_CHUNKS][HASH_SIZE];
    int swarm[2500];
    int peers;
} metadata_tracker;

typedef struct {
    int file_idx;
    metadata_tracker files[MAX_FILES];
} info_tracker;

typedef struct {
    char filename[MAX_FILENAME];
    int segments;
    char hashes[MAX_CHUNKS][HASH_SIZE];
    int downloaded[MAX_CHUNKS];
} metadata_extended;

typedef struct {
    int rank;
    int total_owned_files;
    metadata_extended files[MAX_FILES];
    int desired_file_idx;
    char desired_files[MAX_FILES][MAX_FILENAME];
    int actual_peer[MAX_FILES];
} thread_args;

typedef struct {
    int peer_id;
    char filename[MAX_FILENAME];
    char hash_segment[HASH_SIZE];
} query_request;

MPI_Datatype create_contiguous_mpi_type(size_t struct_size) {
    MPI_Datatype customtype;
    MPI_Type_contiguous(struct_size, MPI_BYTE, &customtype);
    MPI_Type_commit(&customtype);
    return customtype;
}

int equal_strings(char *s1, char *s2) {
    return strcmp(s1, s2) == 0;
}

void *download_thread_func(void *arg) {
    thread_args *args = (thread_args *)arg;
    MPI_Datatype mpi_metadata_tracker_type = create_contiguous_mpi_type(sizeof(metadata_tracker));
    metadata_tracker current_file_tracker_info;
    int rank = args->rank;
    int total_desired_files = args->desired_file_idx;
    int total_downloaded_segments = 0;
    query_request query;
    MPI_Datatype mpi_query = create_contiguous_mpi_type(sizeof(query_request));


    for (int i = 0; i < total_desired_files; i++) {
        char desired_file[MAX_FILENAME];
        memcpy(desired_file, args->desired_files[i], sizeof(desired_file));
        desired_file[MAX_FILENAME - 1] = '\0';
        MPI_Sendrecv(desired_file, MAX_FILENAME, MPI_CHAR, 0, 2, &current_file_tracker_info, 1,
                     mpi_metadata_tracker_type, 0, 2, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        pthread_mutex_lock(&mutex_args);
        memcpy(args->files[args->total_owned_files].filename, desired_file, sizeof(args->files[args->total_owned_files].filename));
        args->files[args->total_owned_files].segments = current_file_tracker_info.segments;

        metadata_extended *current_file = &args->files[args->total_owned_files];
        for (int j = 0; j < current_file_tracker_info.segments; j++) {
            memcpy(current_file->hashes[j], current_file_tracker_info.hashes[j], HASH_SIZE);
            current_file->downloaded[j] = 0;
        }

        args->total_owned_files++;
        pthread_mutex_unlock(&mutex_args);

        int idx = args->total_owned_files - 1;

        int segment_idx = 0;
        while (segment_idx < current_file_tracker_info.segments) {
            int peer_idx = args->actual_peer[i] % current_file_tracker_info.peers;
            int chosen_peer = current_file_tracker_info.swarm[peer_idx];
            args->actual_peer[i]++;

            char anwser[25];

            query.peer_id = rank;
            memcpy(query.filename, desired_file, sizeof(query.filename));
            strcpy(query.hash_segment, current_file_tracker_info.hashes[segment_idx]);

            MPI_Sendrecv(&query, 1, mpi_query, chosen_peer, 999,
                         anwser, 25, MPI_CHAR, chosen_peer, 3, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            if (equal_strings(anwser, "ACK") &&
                (args->files[idx].downloaded[segment_idx] = 1) &&
                (++total_downloaded_segments % 10 == 0)) {
                    MPI_Sendrecv(desired_file, MAX_FILENAME, MPI_CHAR, 0, 2,
                             &current_file_tracker_info, 1, mpi_metadata_tracker_type, 0, 2,
                             MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                }

            if (equal_strings(anwser, "ACK"))
                segment_idx++;
        }

        MPI_Send("I'm seed", MAX_FILENAME, MPI_CHAR, 0, 7, MPI_COMM_WORLD);
        char out[25];
        snprintf(out, sizeof(out), "client%d_%s", args->rank, desired_file);

        FILE *f = fopen(out, "w");
        if (!f) {
            fprintf(stderr, "Error! %s\n", out);
            return;
        }

        for (int j = 0; j < current_file_tracker_info.segments; j++) {
            if (args->files[idx].downloaded[j] == 1) {
                fprintf(f, "%s%s", current_file_tracker_info.hashes[j], (j != current_file_tracker_info.segments - 1) ? "\n" : "");
            }
        }
        fclose(f);
    }

    MPI_Rsend("Done", MAX_FILENAME, MPI_CHAR, 0, 4, MPI_COMM_WORLD);
    MPI_Type_free(&mpi_query);
    return NULL;
}

void *upload_thread_func(void *arg) {
    thread_args *args = (thread_args *)arg;
    MPI_Status status;
    query_request query;
    MPI_Datatype mpi_query = create_contiguous_mpi_type(sizeof(query_request));

    while (1)
    {
        MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
        if (status.MPI_TAG == 999)
        {
            MPI_Recv(&query, 1, mpi_query, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
            char anwser[25] = "NoACK";

            int ok = 1;
            for (int i = 0; i < args->total_owned_files && ok; i++)
            {
                if (equal_strings(args->files[i].filename, query.filename))
                    for (int j = 0; j < args->files[i].segments && ok; j++)
                        if (equal_strings(args->files[i].hashes[j], query.hash_segment) && args->files[i].downloaded[j] == 1)
                        {
                            strcpy(anwser, "ACK");
                            ok = 0;
                        }
            }

            MPI_Send(anwser, 25, MPI_CHAR, status.MPI_SOURCE, 3, MPI_COMM_WORLD);
        }
        else if (status.MPI_TAG == 8)
        {
            MPI_Recv(&query, 1, mpi_query, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
            return NULL;
        }
    }

    MPI_Type_free(&mpi_query);
    return NULL;
}

void tracker(int numtasks, int rank)
{
    MPI_Datatype mpi_peer_info_type = create_contiguous_mpi_type(sizeof(peer_tracker_info));
    MPI_Datatype mpi_metadata_tracker_type = create_contiguous_mpi_type(sizeof(metadata_tracker));

    peer_tracker_info client_data;
    info_tracker tracker_data;
    tracker_data.file_idx = 0;
    char filename[MAX_FILENAME];
    int completed_clients = 0;
    MPI_Status mpi_status;

    int clients_processed = 0;
    while (clients_processed < numtasks - 1) {
        MPI_Recv(&client_data, 1, mpi_peer_info_type, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, &mpi_status);

        int source = mpi_status.MPI_SOURCE;

        for (int j = 0; j < client_data.total_owned_files; j++) {
            int idx = tracker_data.file_idx++;
            memcpy(tracker_data.files[idx].filename, client_data.files[j].filename, sizeof(client_data.files[j].filename));
            tracker_data.files[idx].segments = client_data.files[j].segments;
            memcpy(tracker_data.files[idx].hashes, client_data.files[j].hashes, client_data.files[j].segments * HASH_SIZE);

            tracker_data.files[idx].peers = 0;
            tracker_data.files[idx].swarm[tracker_data.files[idx].peers++] = source;
        }
        clients_processed++;
    }
    MPI_Type_free(&mpi_peer_info_type);

    clients_processed = 1;
    char ack_status[25] = "ACK";
    MPI_Request *requests = malloc((numtasks - 1) * sizeof(MPI_Request));
    int ireq = 0;

    while (clients_processed <= numtasks - 1) {
        MPI_Isend(ack_status, 25, MPI_CHAR, clients_processed, 0, MPI_COMM_WORLD, &requests[ireq++]);
        clients_processed++;
    }
    MPI_Waitall(numtasks - 1, requests, MPI_STATUSES_IGNORE);
    free(requests);


    while (1) {
        MPI_Recv(filename, MAX_FILENAME, MPI_CHAR, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &mpi_status);
        if (equal_strings(filename, "Done")) {
            completed_clients++;
            if (completed_clients == numtasks - 1) {
                query_request query;
                MPI_Datatype mpi_query = create_contiguous_mpi_type(sizeof(query_request));
                for (int i = 1; i <= numtasks - 1; i++)
                    MPI_Send(&query,1 ,mpi_query , i, 8, MPI_COMM_WORLD);
                break;
            }
        } else if (equal_strings(filename, "I'm seed")) {
        } else {
            int idx = -1;
            for (int i = 0; i < tracker_data.file_idx && idx == -1; i++)
                if (equal_strings(tracker_data.files[i].filename, filename))
                    idx = i;

            if (idx != -1) {
                MPI_Send(&tracker_data.files[idx], 1, mpi_metadata_tracker_type, mpi_status.MPI_SOURCE, mpi_status.MPI_TAG, MPI_COMM_WORLD);
                int client_id = mpi_status.MPI_SOURCE;
                int ok = 1;
                for (int i = 0; i < tracker_data.files[idx].peers && ok; i++)
                    if (tracker_data.files[idx].swarm[i] == client_id)
                        ok = 0;
                if (ok)
                    tracker_data.files[idx].swarm[tracker_data.files[idx].peers++] = client_id;
            }
        }
    }

    MPI_Type_free(&mpi_metadata_tracker_type);
}

void read_peer_data(const char *filename, peer_tracker_info *details, thread_args *download) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "Error %s\n", filename);
        exit(-1);
    }

    int file_count;
    fscanf(f, "%d", &file_count);
    details->total_owned_files = file_count;
    download->total_owned_files = file_count;

    for (int i = 0; i < details->total_owned_files; i++) {
        fscanf(f, "%s %d", details->files[i].filename, &details->files[i].segments);
        for (int j = 0; j < details->files[i].segments; j++) {
            fscanf(f, "%s", details->files[i].hashes[j]);
        }

        memcpy(download->files[i].filename, details->files[i].filename, sizeof(details->files[i].filename));
        memcpy(&download->files[i].segments, &details->files[i].segments, sizeof(details->files[i].segments));
        for (int j = 0; j < details->files[i].segments; j++) {
            memcpy(download->files[i].hashes[j], details->files[i].hashes[j], sizeof(details->files[i].hashes[j]));
            download->files[i].downloaded[j] = 1;
        }
    }

    fscanf(f, "%d", &download->desired_file_idx);
    for (int i = 0; i < download->desired_file_idx; i++) {
        fscanf(f, "%s", download->desired_files[i]);
        memset(&download->actual_peer[i], 0, sizeof(int));
    }

    fclose(f);
}

void peer(int numtasks, int rank) {
    pthread_t download_thread;
    pthread_t upload_thread;
    void *status;
    int r;

    char filename[25];
    snprintf(filename, sizeof(filename), "in%d.txt", rank);
    FILE *f = fopen(filename, "r");
    if (!f)
    {
        fprintf(stderr, "Error");
        exit(-1);
    }

    peer_tracker_info details;
    thread_args download;
    thread_args upload;
    download.rank = rank;
    read_peer_data(filename, &details, &download);

    memcpy(&upload, &download, sizeof(thread_args));

    MPI_Datatype mpi_peer_info_type = create_contiguous_mpi_type(sizeof(peer_tracker_info));

    char ack_status[25];

    MPI_Sendrecv(&details, 1, mpi_peer_info_type, 0, 0, ack_status, 25, MPI_CHAR, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    MPI_Type_free(&mpi_peer_info_type);

    if (equal_strings(ack_status, "ACK") == 0)
    {
        return;
    }


    thread_args args;
    memcpy(&args, &download, sizeof(thread_args));
    r = pthread_create(&download_thread, NULL, download_thread_func, (void *)&args);
    if (r) {
        printf("Eroare la crearea thread-ului de download\n");
        exit(-1);
    }

    r = pthread_create(&upload_thread, NULL, upload_thread_func, (void *)&args);
    if (r) {
        printf("Eroare la crearea thread-ului de upload\n");
        exit(-1);
    }

    r = pthread_join(download_thread, &status);
    if (r) {
        printf("Eroare la asteptarea thread-ului de download\n");
        exit(-1);
    }

    r = pthread_join(upload_thread, &status);
    if (r) {
        printf("Eroare la asteptarea thread-ului de upload\n");
        exit(-1);
    }
}

int main(int argc, char *argv[])
{
    int numtasks, rank;

    int provided;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
    if (provided < MPI_THREAD_MULTIPLE) {
        fprintf(stderr, "MPI nu are suport pentru multi-threading\n");
        exit(-1);
    }
    MPI_Comm_size(MPI_COMM_WORLD, &numtasks);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if (rank == 0) {
        tracker(numtasks, rank);
    }
    else {
        peer(numtasks, rank);
    }

    MPI_Finalize();
}
