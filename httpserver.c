#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <pthread.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <string.h>
#include <fcntl.h>
#include <err.h>
#include <errno.h>
#include <stdbool.h>
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>
#include <getopt.h>

#define PORT 8080
#define DEFAULT_NUM_THREADS 4
#define true 1
#define false 0

int offset = 0;
pthread_mutex_t thread_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t pwrite_mutex = PTHREAD_MUTEX_INITIALIZER;
sem_t thread_sem;
size_t thread_in_use = 0;
int log_write = 0;
int num_entries = 0;
int err_entries = 0;
size_t buffer_size = 32768;
typedef struct thread_arg_t
{
  char *header;
  int client_sockd;
  char *log_name;

} ThreadArg;

void *get_request(void *get_arg)
{
  ThreadArg *argp = (ThreadArg *)get_arg;
  char *header_buffer = argp->header;
  int new_socket = argp->client_sockd;
  char *log_name = argp->log_name;

  char *success = (char *)" 200 OK\r\n";
  char *not_found = (char *)" 404 Not found\r\n";
  char *forbidden = (char *)" 403 Forbidden\r\n";
  char *bad_request = (char *)" 400 Bad request\r\n";
  char *get_content_length = (char *)"Content-Length: ";
  char *ending = "\r\n";
  char *validChars = (char *)"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_";
  char *fail_get = (char *)"FAIL: GET ";
  char *success_get = (char *)"GET ";
  char *success_length = (char *)" length ";
  char *log_bad_request = (char *)" HTTP/1.1 --- response 400\n";
  char *log_forbidden = (char *)" HTTP/1.1 --- response 403\n";
  char *log_not_found = (char *)" HTTP/1.1 --- response 404\n";
  char *end_log = (char *)"========\n";
  char *log_new_line = (char *)"\n";
  char *log_space = (char *)" ";
  bool valid_file_name = true;

  char requestType[4] = {0};
  char raw_file_name[750] = {0};
  char adjusted_file_name[750] = {0};
  char client_source[30] = {0};
  int file_name_length = 0;
  int p_offset = 0;

  char byte_padding[9];
  size_t byte = 0;
  sprintf(byte_padding, "%08ld", byte);
  sscanf(header_buffer, "%s", requestType);
  sscanf(header_buffer, "%*s %s", raw_file_name, NULL);
  sscanf(header_buffer, "%*s %*s %s", client_source, NULL);

  //remove the initial slash for the file name
  int j = 0;
  printf("raw file name size: %lu\n", sizeof(raw_file_name));
  for (size_t i = 1; i < sizeof(raw_file_name); ++i)
  {
    if (raw_file_name[i] != 0)
    {
      adjusted_file_name[j] = raw_file_name[i];
      file_name_length++;
    }
    j++;
  }
  printf("adjusted file name: %s\n", adjusted_file_name);
  printf("%d\n", file_name_length);

  char *namingConvention = adjusted_file_name;
  //checking for naming convention is inspired by Joshua Cheung, who taught me how to use strspn to check for matching char in strings
  if (namingConvention[strspn(namingConvention, validChars)] != 0)
  {
    valid_file_name = false;
  }

  // if the length of the textfile is not 27 chars
  if (file_name_length > 27 || !valid_file_name || strcmp(client_source, "HTTP/1.1") != 0)
  {
    char *correct_HTTP = (char *)"HTTP/1.1";
    //lock here
    if (log_write == 1)
    {
      pthread_mutex_lock(&pwrite_mutex);
      p_offset = offset;
      offset = offset + strlen(fail_get) + strlen(raw_file_name) + strlen(log_bad_request) + strlen(end_log);
      ++num_entries;
      ++err_entries;
      pthread_mutex_unlock(&pwrite_mutex);

      int l_fd = open(log_name, O_WRONLY, 0666);
      pwrite(l_fd, fail_get, strlen(fail_get), p_offset);
      p_offset += strlen(fail_get);
      pwrite(l_fd, raw_file_name, strlen(raw_file_name), p_offset);
      p_offset += strlen(raw_file_name);
      pwrite(l_fd, log_bad_request, strlen(log_bad_request), p_offset);
      p_offset += strlen(log_bad_request);
      pwrite(l_fd, end_log, strlen(end_log), p_offset);
      p_offset += strlen(end_log);
      close(l_fd);
    }

    send(new_socket, correct_HTTP, strlen(correct_HTTP), 0);
    send(new_socket, bad_request, strlen(bad_request), 0);
  }
  else
  {
    if (strcmp(adjusted_file_name, "healthcheck") == 0 && log_write == 1)
    {
      printf("client is requesting a healthcheck on log file\n");
      char str_num_entries[10];
      sprintf(str_num_entries, "%u", num_entries);
      char str_err_entries[10];
      sprintf(str_err_entries, "%u", err_entries);

      char healthcheck_response[30];
      strcat(healthcheck_response, str_err_entries);
      strcat(healthcheck_response, log_new_line);
      strcat(healthcheck_response, str_num_entries);

      int healthcheck_length = strlen(healthcheck_response);
      char str_healthcheck_length[30];
      sprintf(str_healthcheck_length, "%u", healthcheck_length);

      send(new_socket, client_source, strlen(client_source), 0);
      send(new_socket, success, strlen(success), 0);
      send(new_socket, get_content_length, strlen(get_content_length), 0);
      send(new_socket, str_healthcheck_length, strlen(str_healthcheck_length), 0);
      send(new_socket, ending, strlen(ending), 0);
      send(new_socket, ending, strlen(ending), 0);
      send(new_socket, healthcheck_response, strlen(healthcheck_response), 0);
    }
    else
    {
      /* code */
      //get file size and convert to char array
      int compare_name = strcmp(adjusted_file_name, "healthcheck");
      struct stat file_type;
      stat(adjusted_file_name, &file_type);
      size_t file_size = file_type.st_size;
      char file_size_arr[750];
      sprintf(file_size_arr, "%zu", file_size);
      int s_file = open(adjusted_file_name, O_RDONLY);
      printf("s_file fd: %d\n", s_file);
      printf("file name: %s\n", adjusted_file_name);
      if (s_file == -1 || compare_name == 0)
      {
        //if it is denied access, one of the errors in open man page
        if (errno == EACCES)
        {
          /* Write to Log file here */
          printf("GET not found\n");
          //lock here
          if (log_write == 1)
          {
            pthread_mutex_lock(&pwrite_mutex);
            p_offset = offset;
            printf("offset before new request: %d\n", offset);
            offset = offset + strlen(fail_get) + strlen(raw_file_name) + strlen(log_forbidden) + strlen(end_log);
            printf("offset after new request: %d\n", offset);
            ++num_entries;
            ++err_entries;
            pthread_mutex_unlock(&pwrite_mutex);

            printf("logging file\n");
            int l_fd = open(log_name, O_WRONLY, 0666);
            pwrite(l_fd, fail_get, strlen(fail_get), p_offset);
            p_offset += strlen(fail_get);
            pwrite(l_fd, raw_file_name, strlen(raw_file_name), p_offset);
            p_offset += strlen(raw_file_name);
            pwrite(l_fd, log_forbidden, strlen(log_forbidden), p_offset);
            p_offset += strlen(log_forbidden);
            pwrite(l_fd, end_log, strlen(end_log), p_offset);
            p_offset += strlen(end_log);
            close(l_fd);
          }

          //send forbidden to socket
          printf("sending responses\n");
          send(new_socket, client_source, strlen(client_source), 0);
          printf("sent client source: %s\n", client_source);
          send(new_socket, forbidden, strlen(forbidden), 0);
          printf("sent forbidden: %s\n", forbidden);
        }
        //other wise it doesn't exist
        else
        {
          /* Write to Log file here */
          //lock here
          printf("log write: %d\nfile name: %s\n", log_write, adjusted_file_name);
          if (log_write == 1 || (log_write == 0 && compare_name == 0))
          {
            pthread_mutex_lock(&pwrite_mutex);
            p_offset = offset;
            offset = offset + strlen(fail_get) + strlen(raw_file_name) + strlen(log_not_found) + strlen(end_log);
            ++num_entries;
            ++err_entries;
            pthread_mutex_unlock(&pwrite_mutex);

            int l_fd = open(log_name, O_WRONLY, 0666);
            pwrite(l_fd, fail_get, strlen(fail_get), p_offset);
            p_offset += strlen(fail_get);
            pwrite(l_fd, raw_file_name, strlen(raw_file_name), p_offset);
            p_offset += strlen(raw_file_name);
            pwrite(l_fd, log_not_found, strlen(log_not_found), p_offset);
            p_offset += strlen(log_not_found);
            pwrite(l_fd, end_log, strlen(end_log), p_offset);
            p_offset += strlen(end_log);
            close(l_fd);
          }

          send(new_socket, client_source, strlen(client_source), 0);
          send(new_socket, not_found, strlen(not_found), 0);
        }
      }
      else
      {

        char *buffer = (char *)calloc(32768, sizeof(char));

        send(new_socket, client_source, strlen(client_source), 0);
        send(new_socket, success, strlen(success), 0);
        send(new_socket, get_content_length, strlen(get_content_length), 0);
        send(new_socket, file_size_arr, strlen(file_size_arr), 0);
        send(new_socket, ending, strlen(ending), 0);
        send(new_socket, ending, strlen(ending), 0);
        printf("GET response sent\n");

        while (true)
        {
          int size_read = read(s_file, buffer, sizeof(buffer));
          if (size_read <= 0)
          {
            break;
          }
          else
          {
            //size += size_read;
            send(new_socket, buffer, size_read, 0);
            free(buffer);
            buffer = (char *)calloc(32768, sizeof(char));
          }
        }
        close(s_file);

        if (log_write == 1)
        {
          char *log_buffer = (char *)calloc(32768, sizeof(char));
          char *hex_buffer = (char *)calloc(40, sizeof(char));
          size_t log_written_size = 0;
          int l_fd = open(log_name, O_WRONLY, 0644);
          //lock here
          pthread_mutex_lock(&pwrite_mutex);
          size_t hex_count = 3 * file_size;
          size_t newline_count = file_size % 20;
          size_t pad_count = 8 * newline_count;
          p_offset = offset;
          offset += strlen(success_get) + strlen(raw_file_name) + strlen(success_length) + strlen(log_new_line) + hex_count + newline_count + pad_count + strlen(log_new_line) + strlen(end_log);
          ++num_entries;
          pthread_mutex_unlock(&pwrite_mutex);

          pwrite(l_fd, success_get, strlen(success_get), p_offset);
          p_offset += strlen(success_get);
          pwrite(l_fd, raw_file_name, strlen(raw_file_name), p_offset);
          p_offset += strlen(raw_file_name);
          pwrite(l_fd, success_length, strlen(success_length), p_offset);
          p_offset += strlen(success_length);
          pwrite(l_fd, file_size_arr, strlen(file_size_arr), p_offset);
          p_offset += strlen(file_size_arr);
          pwrite(l_fd, log_new_line, strlen(log_new_line), p_offset);
          p_offset += strlen(log_new_line);

          int fd = open(adjusted_file_name, O_RDONLY);
          while (true)
          {
            int size_read = read(fd, log_buffer, sizeof(log_buffer));
            if (size_read <= 0)
            {
              break;
            }
            else
            {
              for (int i = 0; i < size_read; ++i)
              {

                if (log_written_size % 20 == 0)
                {
                  if (log_written_size != 0)
                  {
                    pwrite(l_fd, log_new_line, strlen(log_new_line), p_offset);
                    p_offset += strlen(log_new_line);
                  }
                  pwrite(l_fd, byte_padding, strlen(byte_padding), p_offset);
                  p_offset += strlen(byte_padding);
                  pwrite(l_fd, log_space, strlen(log_space), p_offset);
                  p_offset += strlen(log_space);
                  byte += 20;
                  sprintf(byte_padding, "%08ld", byte);
                }
                sprintf(hex_buffer, " %02x", log_buffer[i]);
                pwrite(l_fd, hex_buffer, strlen(hex_buffer), p_offset);
                p_offset += strlen(hex_buffer);
                free(hex_buffer);
                hex_buffer = (char *)calloc(40, sizeof(char));
                log_written_size += 1;
              }
              free(log_buffer);
              log_buffer = (char *)calloc(32768, sizeof(char));
            }
          }
          free(log_buffer);
          printf("file size: %ld\n", file_size);
          if (file_size > 0)
          {
            printf("file size is greater than 0\n");
            pwrite(l_fd, log_new_line, strlen(log_new_line), p_offset);
            p_offset += strlen(log_new_line);
          }
          printf("logging end log\n");
          pwrite(l_fd, end_log, strlen(end_log), p_offset);
          p_offset += strlen(end_log);
          offset = p_offset;

          close(l_fd);
        }

        free(buffer);
      }
    }
  }
  close(new_socket);
  sem_post(&thread_sem);
  pthread_mutex_lock(&thread_mutex);
  thread_in_use--;
  pthread_mutex_unlock(&thread_mutex);
  return NULL;
}

//discussed high-level logic with Joshua Cheung to structure of the process of PUT
void *put_request(void *put_arg)
{
  ThreadArg *argp = (ThreadArg *)put_arg;
  char *header_buffer = argp->header;
  int new_socket = argp->client_sockd;
  char *log_name = argp->log_name;

  char *success = (char *)" 200 OK\r\n";
  char *forbidden = (char *)" 403 Forbidden\r\n";
  char *created = (char *)" 201 Created\r\n";
  char *bad_request = (char *)" 400 Bad Request\r\n";
  char *put_content_length = (char *)"Content-Length: 0\r\n";
  char *ending = (char *)"\r\n";
  char *validChars = (char *)"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_";
  bool valid_name = true;
  const size_t buffer_size = 32768;

  char adjusted_file_name[500] = {0};
  char raw_file_name[500] = {0};
  char client_source[20] = {0};
  int readFileLength;

  // content length of the file
  char content_length[20] = {0};
  char content_length_phrase[20] = {0};

  bool writeFailed = false;

  char *fileBuffer = (char *)calloc(buffer_size, sizeof(char));
  char *hex_buffer = (char *)calloc(40, sizeof(char));
  bool denied = false;
  bool file_created = false;
  int name_length = 0;

  int int_content_length;
  bool missing_content_length = false;

  //log file stuff
  char *fail_get = (char *)"FAIL: PUT ";
  char *success_get = (char *)"PUT ";
  char *success_length = (char *)" length ";
  char *log_bad_request = (char *)" HTTP/1.1 --- response 400\n";
  char *log_forbidden = (char *)" HTTP/1.1 --- response 403\n";
  char *end_log = (char *)"========\n";
  char *log_new_line = (char *)"\n";
  char *log_space = (char *)" ";
  int p_offset = 0;
  char byte_padding[9];
  size_t byte = 0;
  sprintf(byte_padding, "%08ld", byte);

  // parse header for textfile name, server type, content length, and for content length string
  sscanf(header_buffer, "%*s %s", raw_file_name, NULL);
  sscanf(header_buffer, "%*s %*s %s", client_source, NULL);
  sscanf(header_buffer, "%*s %*s %*s %*s %*s %*s %*s %*s %*s %s", content_length_phrase, NULL);
  sscanf(header_buffer, "%*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %s", content_length, NULL);
  int j = 0;

  // need to check to make sure there is a contentLength in the header, or else it is an invalid put
  for (size_t i = 1; i < sizeof(raw_file_name); ++i)
  {
    if (raw_file_name[i] != 0)
    {
      adjusted_file_name[j] = raw_file_name[i];
      name_length++;
    }
    j++;
  }
  int_content_length = atoi(content_length);

  char *namingConvention = adjusted_file_name;
  if (namingConvention[strspn(namingConvention, validChars)] != 0)
  {
    valid_name = false;
  }

  //send(new_socket, client_source, strlen(client_source), 0);

  if (missing_content_length == true || name_length > 27 || valid_name == false || strcmp(client_source, "HTTP/1.1") != 0)
  {
    /* Write to Log file here */
    printf("HTTP version: %s\n", client_source);
    char *correct_HTTP = (char *)"HTTP/1.1";

    if (log_write == 1)
    {
      int l_fd = open(log_name, O_WRONLY, 0666);
      pthread_mutex_lock(&pwrite_mutex);
      p_offset = offset;
      offset += strlen(fail_get) + strlen(raw_file_name) + strlen(log_bad_request) + strlen(end_log);
      ++num_entries;
      ++err_entries;
      pthread_mutex_unlock(&pwrite_mutex);

      pwrite(l_fd, fail_get, strlen(fail_get), p_offset);
      p_offset += strlen(fail_get);
      pwrite(l_fd, raw_file_name, strlen(raw_file_name), p_offset);
      p_offset += strlen(raw_file_name);
      pwrite(l_fd, log_bad_request, strlen(log_bad_request), p_offset);
      p_offset += strlen(log_bad_request);
      pwrite(l_fd, end_log, strlen(end_log), p_offset);
      p_offset += strlen(end_log);
      close(l_fd);
    }

    send(new_socket, correct_HTTP, strlen(correct_HTTP), 0);
    printf("response to send: %s%s\n", client_source, bad_request);
    send(new_socket, bad_request, strlen(bad_request), 0);
  }
  else
  {
    int fileDescriptor = open(adjusted_file_name, O_WRONLY);

    int compare_name = strcmp(adjusted_file_name, "healthcheck");

    if (fileDescriptor < 0 || compare_name == 0)
    {
      // send error code 400 forbidden
      if (errno == EACCES || compare_name == 0)
      {
        /* Write to Log file here */
        if (log_write == 1)
        {
          printf("forbidden\n");
          int l_fd = open(log_name, O_WRONLY, 0666);
          pthread_mutex_lock(&pwrite_mutex);
          p_offset = offset;
          offset += strlen(fail_get) + strlen(raw_file_name) + strlen(log_forbidden) + strlen(end_log);
          ++num_entries;
          ++err_entries;
          pthread_mutex_unlock(&pwrite_mutex);

          pwrite(l_fd, fail_get, strlen(fail_get), p_offset);
          p_offset += strlen(fail_get);
          pwrite(l_fd, raw_file_name, strlen(raw_file_name), p_offset);
          p_offset += strlen(raw_file_name);
          pwrite(l_fd, log_forbidden, strlen(log_forbidden), p_offset);
          p_offset += strlen(log_forbidden);
          pwrite(l_fd, end_log, strlen(end_log), p_offset);
          p_offset += strlen(end_log);
          close(l_fd);
        }

        printf("sending forbidden request\n");
        send(new_socket, client_source, strlen(client_source), 0);
        send(new_socket, forbidden, strlen(forbidden), 0);
        denied = true;
      }
      else
      {
        fileDescriptor = open(adjusted_file_name, O_CREAT | O_WRONLY, 0666);
        file_created = true;
      }
    }
    else
    {
      // if the file already exists, we need to override existing file
      remove(adjusted_file_name);
      fileDescriptor = open(adjusted_file_name, O_CREAT | O_WRONLY, 0666);
    }

    if (denied == false)
    {

      size_t read_size = 0;

      //printf("p_offset after log_new_line: %d\n", p_offset);
      while (true)
      {
        fcntl(new_socket, F_SETFL, O_NONBLOCK);
        readFileLength = read(new_socket, fileBuffer, sizeof(fileBuffer));
        //printf("readfileLength: %d\nread_size: %d\nint content_length: %d\n", readFileLength, read_size, int_content_length);
        printf("buffer content: %s\n", fileBuffer);
        if (readFileLength != -1)
        {
          //if the file is not empty
          int writeToFile = write(fileDescriptor, fileBuffer, readFileLength);

          if (writeToFile < 0)
          {
            if (errno == EACCES)
            {
              writeFailed = true;
            }
          }
          read_size += readFileLength;
          free(fileBuffer);
          fileBuffer = (char *)calloc(buffer_size, sizeof(char));
        }

        if (readFileLength == 0 || read_size == int_content_length)
        {
          printf("readFileLength %d\nread size %ld\nint content length %d\n", readFileLength, read_size, int_content_length);
          printf("Nothing left to read, break\n");
          break;
        }
      }
      printf("exited while loop\n");
      free(fileBuffer);
      close(fileDescriptor);
      //log here
      if (log_write == 1)
      {
        size_t log_written_size = 0;
        int l_fd = open(log_name, O_WRONLY, 0666);

        printf("Locking for logging\n");
        pthread_mutex_lock(&pwrite_mutex);
        struct stat file_type;
        stat(adjusted_file_name, &file_type);
        size_t file_size = file_type.st_size;

        size_t hex_count = 3 * file_size;
        size_t newline_count = file_size % 20;
        size_t pad_count = 8 * newline_count;
        p_offset = offset;
        offset += strlen(success_get) + strlen(raw_file_name) + strlen(success_length) + strlen(content_length) + strlen(log_new_line) + hex_count + newline_count + pad_count + strlen(log_new_line) + strlen(end_log);
        ++num_entries;
        pthread_mutex_unlock(&pwrite_mutex);

        pwrite(l_fd, success_get, strlen(success_get), p_offset);
        p_offset += strlen(success_get);
        pwrite(l_fd, raw_file_name, strlen(raw_file_name), p_offset);
        p_offset += strlen(raw_file_name);
        pwrite(l_fd, success_length, strlen(success_length), p_offset);
        p_offset += strlen(success_length);
        pwrite(l_fd, content_length, strlen(content_length), p_offset);
        p_offset += strlen(content_length);
        pwrite(l_fd, log_new_line, strlen(log_new_line), p_offset);
        p_offset += strlen(log_new_line);
        int s_file = open(adjusted_file_name, O_RDONLY);
        char *buffer = (char *)calloc(32768, sizeof(char));
        while (true)
        {
          int size_read = read(s_file, buffer, sizeof(buffer));
          if (size_read <= 0)
          {
            break;
          }
          else
          {
            //size += size_read;
            for (int i = 0; i < size_read; ++i)
            {
              if (log_written_size % 20 == 0)
              {
                if (log_written_size != 0)
                {
                  pwrite(l_fd, log_new_line, strlen(log_new_line), p_offset);
                  p_offset += strlen(log_new_line);
                }
                pwrite(l_fd, byte_padding, strlen(byte_padding), p_offset);
                p_offset += strlen(byte_padding);
                pwrite(l_fd, log_space, strlen(log_space), p_offset);
                p_offset += strlen(log_space);
                byte += 20;
                sprintf(byte_padding, "%08ld", byte);
              }
              sprintf(hex_buffer, " %02x", buffer[i]);
              pwrite(l_fd, hex_buffer, strlen(hex_buffer), p_offset);
              p_offset += strlen(hex_buffer);
              free(hex_buffer);
              hex_buffer = (char *)calloc(40, sizeof(char));
              log_written_size += 1;
            }
            free(buffer);
            buffer = (char *)calloc(32768, sizeof(char));
          }
        }
        free(buffer);
        if (file_size > 0)
        {
          pwrite(l_fd, log_new_line, strlen(log_new_line), p_offset);
          p_offset += strlen(log_new_line);
        }
        pwrite(l_fd, end_log, strlen(end_log), p_offset);
        p_offset += strlen(end_log);
        offset = p_offset;
        close(l_fd);
        printf("unlocked mutex\n");
      }

      // send error code 403 forbidden
      if (writeFailed == true)
      {
        if (log_write == 1)
        {
          int l_fd = open(log_name, O_WRONLY, 0666);

          pthread_mutex_lock(&pwrite_mutex);
          p_offset = offset;
          offset += strlen(fail_get) + strlen(raw_file_name) + strlen(log_forbidden) + strlen(end_log);
          ++num_entries;
          ++err_entries;
          pthread_mutex_unlock(&pwrite_mutex);

          pwrite(l_fd, fail_get, strlen(fail_get), p_offset);
          p_offset += strlen(fail_get);
          pwrite(l_fd, raw_file_name, strlen(raw_file_name), p_offset);
          p_offset += strlen(raw_file_name);
          pwrite(l_fd, log_forbidden, strlen(log_forbidden), p_offset);
          p_offset += strlen(log_forbidden);
          pwrite(l_fd, end_log, strlen(end_log), p_offset);
          p_offset += strlen(end_log);
          close(l_fd);
        }

        send(new_socket, client_source, strlen(client_source), 0);
        send(new_socket, forbidden, strlen(forbidden), 0);
      }
      // send error code 201 created
      else if (file_created == true)
      {

        send(new_socket, client_source, strlen(client_source), 0);
        send(new_socket, created, strlen(created), 0);
        send(new_socket, put_content_length, strlen(put_content_length), 0);
        send(new_socket, ending, strlen(ending), 0);
      }
      else
      {
        send(new_socket, client_source, strlen(client_source), 0);
        send(new_socket, success, strlen(success), 0);
        send(new_socket, put_content_length, strlen(put_content_length), 0);
        send(new_socket, ending, strlen(ending), 0);
      }
    }
  }
  close(new_socket);
  sem_post(&thread_sem);
  pthread_mutex_lock(&thread_mutex);
  thread_in_use--;
  pthread_mutex_unlock(&thread_mutex);
  return NULL;
}
void *head_request(void *put_arg)
{
  ThreadArg *argp = (ThreadArg *)put_arg;
  char *header_buffer = argp->header;
  int new_socket = argp->client_sockd;
  char *log_name = argp->log_name;

  char *success = (char *)" 200 OK\r\n";
  char *not_found = (char *)" 404 Not found\r\n";
  char *forbidden = (char *)" 403 Forbidden\r\n";
  char *bad_request = (char *)" 400 Bad request\r\n";
  char *get_content_length = (char *)"Content-Length: ";
  char *ending = "\r\n";
  char *validChars = (char *)"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_";
  bool valid_file_name = true;

  char *fail_get = (char *)"FAIL: HEAD ";
  char *success_get = (char *)"HEAD ";
  char *success_length = (char *)" length ";
  char *log_bad_request = (char *)" HTTP/1.1 --- response 400\n";
  char *log_forbidden = (char *)" HTTP/1.1 --- response 403\n";
  char *log_not_found = (char *)" HTTP/1.1 --- response 404\n";
  char *end_log = (char *)"========\n";
  char *log_new_line = (char *)"\n";

  char raw_file_name[750] = {0};
  char adjusted_file_name[750] = {0};
  char client_source[20] = {0};
  int file_name_length = 0;
  int p_offset = 0;

  sscanf(header_buffer, "%*s %s", raw_file_name, NULL);
  sscanf(header_buffer, "%*s %*s %s", client_source, NULL);
  //send(new_socket, client_source, strlen(client_source), 0);

  //remove the initial slash for the file name
  int j = 0;
  for (size_t i = 1; i < sizeof(raw_file_name); ++i)
  {
    if (raw_file_name[i] != 0)
    {
      adjusted_file_name[j] = raw_file_name[i];
      file_name_length++;
    }
    j++;
  }
  char *namingConvention = adjusted_file_name;
  //checking for naming convention is inspired by Joshua Cheung, who taught me how to use strspn to check for matching char in strings
  if (namingConvention[strspn(namingConvention, validChars)] != 0)
  {
    valid_file_name = false;
  }

  // if the length of the textfile is not 27 chars
  if (file_name_length > 27 || !valid_file_name || strcmp(client_source, "HTTP/1.1") != 0)
  {
    char *correct_HTTP = (char *)"HTTP/1.1";
    if (log_write == 1)
    {

      int l_fd = open(log_name, O_WRONLY, 0666);

      pthread_mutex_lock(&pwrite_mutex);
      p_offset = offset;
      offset += strlen(fail_get) + strlen(raw_file_name) + strlen(log_bad_request) + strlen(end_log);
      ++num_entries;
      ++err_entries;
      pthread_mutex_unlock(&pwrite_mutex);

      pwrite(l_fd, fail_get, strlen(fail_get), p_offset);
      p_offset += strlen(fail_get);
      pwrite(l_fd, raw_file_name, strlen(raw_file_name), p_offset);
      p_offset += strlen(raw_file_name);
      pwrite(l_fd, log_bad_request, strlen(log_bad_request), p_offset);
      p_offset += strlen(log_bad_request);
      pwrite(l_fd, end_log, strlen(end_log), p_offset);
      p_offset += strlen(end_log);
      close(l_fd);
    }

    send(new_socket, correct_HTTP, strlen(correct_HTTP), 0);
    send(new_socket, bad_request, strlen(bad_request), 0);
  }
  else
  {
    //get file size and convert to char array
    struct stat file_type;
    stat(adjusted_file_name, &file_type);
    size_t file_size = file_type.st_size;
    char file_size_arr[750];
    sprintf(file_size_arr, "%zu", file_size);
    int permission = file_type.st_mode;
    if (permission & S_IRUSR || permission & S_IWUSR)
      printf("This file has read or write permission\n");
    int s_file = open(adjusted_file_name, O_RDONLY);

    if (s_file == -1 || strcmp(adjusted_file_name, "healthcheck") == 0)
    {
      //if it is denied access, one of the errors in open man page
      if (errno == EACCES || strcmp(adjusted_file_name, "healthcheck") == 0)
      {
        if (log_write == 1)
        {
          int l_fd = open(log_name, O_WRONLY, 0666);

          pthread_mutex_lock(&pwrite_mutex);
          p_offset = offset;
          offset += strlen(fail_get) + strlen(raw_file_name) + strlen(log_forbidden) + strlen(end_log);
          ++num_entries;
          ++err_entries;
          pthread_mutex_unlock(&pwrite_mutex);

          pwrite(l_fd, fail_get, strlen(fail_get), p_offset);
          p_offset += strlen(fail_get);
          pwrite(l_fd, raw_file_name, strlen(raw_file_name), p_offset);
          p_offset += strlen(raw_file_name);
          pwrite(l_fd, log_forbidden, strlen(log_forbidden), p_offset);
          p_offset += strlen(log_forbidden);
          pwrite(l_fd, end_log, strlen(end_log), p_offset);
          p_offset += strlen(end_log);
          close(l_fd);
        }

        send(new_socket, client_source, strlen(client_source), 0);
        send(new_socket, forbidden, strlen(forbidden), 0);
      }
      //other wise it doesn't exist
      else
      {
        if (log_write == 1)
        {
          int l_fd = open(log_name, O_WRONLY, 0666);

          pthread_mutex_lock(&pwrite_mutex);
          p_offset = offset;
          offset += strlen(fail_get) + strlen(raw_file_name) + strlen(log_not_found) + strlen(end_log);
          ++num_entries;
          ++err_entries;
          pwrite(l_fd, fail_get, strlen(fail_get), p_offset);
          pthread_mutex_unlock(&pwrite_mutex);

          p_offset += strlen(fail_get);
          pwrite(l_fd, raw_file_name, strlen(raw_file_name), p_offset);
          p_offset += strlen(raw_file_name);
          pwrite(l_fd, log_not_found, strlen(log_not_found), p_offset);
          p_offset += strlen(log_not_found);
          pwrite(l_fd, end_log, strlen(end_log), p_offset);
          p_offset += strlen(end_log);
          close(l_fd);
        }

        send(new_socket, client_source, strlen(client_source), 0);
        send(new_socket, not_found, strlen(not_found), 0);
      }
    }
    else
    {
      if (log_write == 1)
      {
        int l_fd = open(log_name, O_WRONLY, 0666);
        pthread_mutex_lock(&pwrite_mutex);
        p_offset = offset;
        offset += strlen(success_get) + strlen(raw_file_name) + strlen(success_length) + strlen(file_size_arr) + strlen(log_new_line) + strlen(end_log);
        ++num_entries;
        pthread_mutex_unlock(&pwrite_mutex);

        pwrite(l_fd, success_get, strlen(success_get), p_offset);
        p_offset += strlen(success_get);
        pwrite(l_fd, raw_file_name, strlen(raw_file_name), p_offset);
        p_offset += strlen(raw_file_name);
        pwrite(l_fd, success_length, strlen(success_length), p_offset);
        p_offset += strlen(success_length);
        pwrite(l_fd, file_size_arr, strlen(file_size_arr), p_offset);
        p_offset += strlen(file_size_arr);
        pwrite(l_fd, log_new_line, strlen(log_new_line), p_offset);
        p_offset += strlen(log_new_line);
        pwrite(l_fd, end_log, strlen(end_log), p_offset);
        p_offset += strlen(end_log);
        close(l_fd);
      }

      send(new_socket, client_source, strlen(client_source), 0);           //HTTP/1.1
      send(new_socket, success, strlen(success), 0);                       // 200 OK\r\n
      send(new_socket, get_content_length, strlen(get_content_length), 0); //Content-Length:
      send(new_socket, file_size_arr, strlen(file_size_arr), 0);           //file size
      send(new_socket, ending, strlen(ending), 0);                         //\r\n
      send(new_socket, ending, strlen(ending), 0);                         //\r\n
    }
    close(s_file);
  }
  close(new_socket);
  return NULL;
}

int main(int argc, char *argv[])
{
  /* create sockaddr_in with server information */
  //char* port = 8080;/* server operating port */

  size_t valread;
  int num_threads = 4;
  char log_name[125];
  char *header_buffer = (char *)malloc(sizeof(char) * 4096);

  int opt;
  char *port_num;

  // put ':' in the starting of the
  // string so that program can
  //distinguish between '?' and ':'
  while ((opt = getopt(argc, argv, "N:l:")) != -1)
  {
    switch (opt)
    {
    case 'N':
      printf("N flag raised\n");
      if (optarg == NULL)
      {
        num_threads = DEFAULT_NUM_THREADS;
      }
      else
      {
        num_threads = atoi(optarg);
      }

      break;
    case 'l':
      if (optarg == NULL)
      {
        printf("no log\n");
        log_write = 0;
      }
      else
      {
        /* code */
        log_write = 1;
        printf("l flag raised\n");
        strcpy(log_name, optarg);
        int l_fd = open(log_name, O_CREAT | O_TRUNC, 0666);
        close(l_fd);
        printf("log_name: %s\n", log_name);
        break;
      }
    }
  }
  printf("num_threads: %d\n", num_threads);
  sem_init(&thread_sem, 0, num_threads);
  /* for (; optind < argc; optind++)
  {
    printf("extra arguments: %s\n", argv[optind]);
    
  } */
  port_num = argv[optind];

  printf("port num: %s\n", port_num);
  struct sockaddr_in server_addr;
  memset(&server_addr, 0, sizeof server_addr);
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(atoi(port_num));
  server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  socklen_t ai_addrlen = sizeof server_addr;

  /* create server socket */
  int server_sockd = socket(AF_INET, SOCK_STREAM, 0);
  /* configure server socket */
  int enable = 1;
  /* this allows you to avoid 'Bind: Address Already in Use' error */
  int ret = setsockopt(server_sockd, SOL_SOCKET, SO_REUSEADDR,
                       &enable, sizeof(enable));
  /* bind server address to socket that is open */
  ret = bind(server_sockd, (struct sockaddr *)&server_addr, ai_addrlen);
  /* listen for incoming connctions */
  ret = listen(server_sockd, SOMAXCONN); /* 5 should be enough, if not use SOMAXCONN */
  /* connecting with a client */
  struct sockaddr client_addr;
  socklen_t client_addrlen = sizeof(client_addr);

  pthread_t thread_pool[num_threads];

  while (true)
  {
    size_t client_sockd = accept(server_sockd, &client_addr, &client_addrlen);
    if (client_sockd == -1)
    {
      perror("client_sockd");
      exit(EXIT_FAILURE);
    }
    /* remember errors happen */
    //initial test of server response

    valread = read(client_sockd, header_buffer, 4096);
    if (valread == -1)
    {
      perror("read header");
      exit(EXIT_FAILURE);
    }
    //header_parser(header_buffer, client_sockd);

    //Header_Parser
    char *requestType = (char *)malloc(sizeof(char) * 4);

    sscanf(header_buffer, "%s", requestType);

    const char *get = "GET";
    const char *put = "PUT";
    const char *head = "HEAD";

    int isGet = strcmp(requestType, get);
    int isPut = strcmp(requestType, put);
    int isHead = strcmp(requestType, head);

    ThreadArg func_arg;
    func_arg.header = header_buffer;
    func_arg.client_sockd = client_sockd;
    func_arg.log_name = log_name;

    if (isGet == 0)
    {
      printf("This is a get function with arg struct paramenter\n");
      //get_request(&func_arg);
      sem_wait(&thread_sem);
      for (int j = 0; j < num_threads; j++)
      {
        pthread_mutex_lock(&thread_mutex);
        //Dequeue and Enqueue from here
        thread_in_use++;
        pthread_mutex_unlock(&thread_mutex);
        //if pthread_create is successful, break from this for loop
        int thread_created = pthread_create(&thread_pool[j], NULL, get_request, &func_arg);
        if (thread_created != -1)
        {
          break;
        }
      }
    }
    else if (isPut == 0)
    {
      sem_wait(&thread_sem);
      for (int j = 0; j < num_threads; j++)
      {
        pthread_mutex_lock(&thread_mutex);
        //Enqueue and Dequeue from here
        thread_in_use++;
        pthread_mutex_unlock(&thread_mutex);
        //if pthread_create is successful, break from this for loop
        int thread_created = pthread_create(&thread_pool[j], NULL, put_request, &func_arg);
        if (thread_created != -1)
        {
          break;
        }
      }
      //put_request(&func_arg);
    }
    else if (isHead == 0)
    {
      sem_wait(&thread_sem);
      for (int j = 0; j < num_threads; j++)
      {
        pthread_mutex_lock(&thread_mutex);
        //Enqueue and Dequeue from here
        thread_in_use++;
        pthread_mutex_unlock(&thread_mutex);
        //if pthread_create is successful, break from this for loop
        int thread_created = pthread_create(&thread_pool[j], NULL, head_request, &func_arg);
        if (thread_created != -1)
        {
          break;
        }
      }
      //head_request(&func_arg);
    }
    else
    {
      char *bad_request = (char *)" 400 Bad Request\r\n";
      char *correct_HTTP = (char *)"HTTP/1.1";
      char *fail = (char *)"Fail: ";
      char *space = (char *)" ";
      char *log_bad_request = (char *)" HTTP/1.1 --- response 400\n";
      char *end_log = (char *)"========\n";
      char raw_file_name[750] = {0};
      sscanf(header_buffer, "%*s %s", raw_file_name, NULL);
      printf("log write: %d\n", log_write);
      if (log_write == 1)
      {
        printf("log name specified");
        int l_fd = open(log_name, O_WRONLY, 0666);
        int p_offset = offset;
        offset += strlen(fail) + strlen(requestType) + strlen(space) + strlen(raw_file_name) + strlen(log_bad_request) + strlen(end_log);
        pwrite(l_fd, fail, strlen(fail), p_offset);
        p_offset += strlen(fail);
        pwrite(l_fd, requestType, strlen(requestType), p_offset);
        p_offset += strlen(requestType);
        pwrite(l_fd, space, strlen(space), p_offset);
        p_offset += strlen(space);
        pwrite(l_fd, raw_file_name, strlen(raw_file_name), p_offset);
        p_offset += strlen(raw_file_name);
        pwrite(l_fd, log_bad_request, strlen(log_bad_request), p_offset);
        p_offset += strlen(log_bad_request);
        pwrite(l_fd, end_log, strlen(end_log), p_offset);
        p_offset += strlen(end_log);
        close(l_fd);
      }
      send(client_sockd, correct_HTTP, strlen(correct_HTTP), 0);
      send(client_sockd, bad_request, strlen(bad_request), 0);
    }

    free(requestType);

    for (int j = 0; j < thread_in_use; j++)
    {
      pthread_join(thread_pool[j], NULL);
    }
  }

  free(header_buffer);
  return 0;
}
