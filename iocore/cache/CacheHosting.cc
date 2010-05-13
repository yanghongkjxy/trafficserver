/** @file

  A brief file description

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

#include "P_Cache.h"

extern int gndisks;

matcher_tags CacheHosting_tags = {
  "hostname", "domain"
};


bool alarmAlready = false;

/*************************************************************
 *   Begin class HostMatcher
 *************************************************************/

CacheHostMatcher::CacheHostMatcher(const char *name, const char *filename, int typ):
data_array(NULL),
array_len(-1),
num_el(-1),
matcher_name(name),
file_name(filename),
type(typ)
{
  host_lookup = NEW(new HostLookup(name));
}

CacheHostMatcher::~CacheHostMatcher()
{

  delete host_lookup;
  delete[]data_array;
}

//
// template <class Data,class Result>
// void HostMatcher<Data,Result>::Print()
//
//  Debugging Method
//
void
CacheHostMatcher::Print()
{

  printf("\tHost/Domain Matcher with %d elements\n", num_el);
  host_lookup->Print(PrintFunc);
}

//
// template <class Data,class Result>
// void CacheHostMatcher::PrintFunc(void* opaque_data)
//
//  Debugging Method
//
void
CacheHostMatcher::PrintFunc(void *opaque_data)
{
  CacheHostRecord *d = (CacheHostRecord *) opaque_data;
  d->Print();
}

// void CacheHostMatcher::AllocateSpace(int num_entries)
//
//  Allocates the the HostLeaf and Data arrays
//
void
CacheHostMatcher::AllocateSpace(int num_entries)
{
  // Should not have been allocated before
  ink_assert(array_len == -1);

  host_lookup->AllocateSpace(num_entries);

  data_array = NEW(new CacheHostRecord[num_entries]);

  array_len = num_entries;
  num_el = 0;
}

// void CacheHostMatcher::Match(RD* rdata, Result* result)
//
//  Searches our tree and updates argresult for each element matching
//    arg hostname
//
void
CacheHostMatcher::Match(char *rdata, int rlen, CacheHostResult * result)
{

  void *opaque_ptr;
  CacheHostRecord *data_ptr;
  bool r;

  // Check to see if there is any work to do before makeing
  //   the stirng copy
  if (num_el <= 0) {
    return;
  }

  if (rlen == 0)
    return;
  char *data = (char *) xmalloc(rlen + 1);
  memcpy(data, rdata, rlen);
  *(data + rlen) = '\0';
  HostLookupState s;

  r = host_lookup->MatchFirst(data, &s, &opaque_ptr);

  while (r == true) {
    ink_assert(opaque_ptr != NULL);
    data_ptr = (CacheHostRecord *) opaque_ptr;
    data_ptr->UpdateMatch(result, data);

    r = host_lookup->MatchNext(&s, &opaque_ptr);
  }
  xfree(data);
}

//
// char* CacheHostMatcher::NewEntry(bool domain_record,
//          char* match_data, char* match_info, int line_num)
//
//   Creates a new host/domain record
//

void
CacheHostMatcher::NewEntry(matcher_line * line_info)
{

  CacheHostRecord *cur_d;
  int errNo;
  char *match_data;

  // Make sure space has been allocated
  ink_assert(num_el >= 0);
  ink_assert(array_len >= 0);

  // Make sure we do not overrun the array;
  ink_assert(num_el < array_len);

  match_data = line_info->line[1][line_info->dest_entry];

  // Make sure that the line_info is not bogus
  ink_assert(line_info->dest_entry < MATCHER_MAX_TOKENS);
  ink_assert(match_data != NULL);

  // Remove our consumed label from the parsed line
  if (line_info->dest_entry < MATCHER_MAX_TOKENS)
    line_info->line[0][line_info->dest_entry] = NULL;
  line_info->num_el--;

  // Fill in the parameter info
  cur_d = data_array + num_el;
  errNo = cur_d->Init(line_info, type);

  if (errNo) {
    // There was a problem so undo the effects this function
    memset(cur_d, 0, sizeof(CacheHostRecord));
    return;
  }
  Debug("cache_hosting", "hostname: %s, host record: %xd", match_data, cur_d);
  // Fill in the matching info
  host_lookup->NewEntry(match_data, (line_info->type == MATCH_DOMAIN) ? true : false, cur_d);

  num_el++;
  return;
}

/*************************************************************
 *   End class HostMatcher
 *************************************************************/

CacheHostTable::CacheHostTable(Cache * c, int typ)
{


  config_tags = &CacheHosting_tags;
  ink_assert(config_tags != NULL);

  type = typ;
  cache = c;
  matcher_name = "[CacheHosting]";;
  config_file_path[0] = '\0';
  char *config_file = NULL;
  IOCORE_ReadConfigStringAlloc(config_file, "proxy.config.cache.hosting_filename");
  ink_release_assert(config_file != NULL);
  ink_strncpy(config_file_path, cache_system_config_directory, sizeof(config_file_path));
  strncat(config_file_path, DIR_SEP, (sizeof(config_file_path) - strlen(config_file_path) - 1));
  strncat(config_file_path, config_file, (sizeof(config_file_path) - strlen(config_file_path) - 1));
  xfree(config_file);
  hostMatch = NULL;

  m_numEntries = this->BuildTable();
}

CacheHostTable::~CacheHostTable()
{

  if (hostMatch != NULL) {
    delete hostMatch;
  }
}

// void ControlMatcher<Data, Result>::Print()
//
//   Debugging method
//
void
CacheHostTable::Print()
{
  printf("Control Matcher Table: %s\n", matcher_name);
  if (hostMatch != NULL) {
    hostMatch->Print();
  }
}


// void ControlMatcher<Data, Result>::Match(RD* rdata
//                                          Result* result)
//
//   Queries each table for the Result*
//
void
CacheHostTable::Match(char *rdata, int rlen, CacheHostResult * result)
{

  hostMatch->Match(rdata, rlen, result);
}

int
CacheHostTable::config_callback(const char *name, RecDataT data_type, RecData data, void *cookie)
{
  NOWARN_UNUSED(name);
  NOWARN_UNUSED(data);
  NOWARN_UNUSED(data_type);
  CacheHostTable **ppt = (CacheHostTable **) cookie;
  eventProcessor.schedule_imm(NEW(new CacheHostTableConfig(ppt)));
  return 0;
}

int fstat_wrapper(int fd, struct stat *s);

// int ControlMatcher::BuildTable() {
//
//    Reads the cache.config file and build the records array
//      from it
//
int
CacheHostTable::BuildTableFromString(char *file_buf)
{
  // Table build locals
  Tokenizer bufTok("\n");
  tok_iter_state i_state;
  const char *tmp;
  matcher_line *first = NULL;
  matcher_line *current;
  matcher_line *last = NULL;
  int line_num = 0;
  int second_pass = 0;
  int numEntries = 0;
  char errBuf[1024];
  const char *errPtr = NULL;

  // type counts
  int hostDomain = 0;

  if (bufTok.Initialize(file_buf, SHARE_TOKS | ALLOW_EMPTY_TOKS) == 0) {
    // We have an empty file
    /* no hosting customers -- put all the partitions in the
       generic table */
    if (gen_host_rec.Init(type))
      Warning("Problems encountered while initializing the Generic Partition");
    return 0;
  }
  // First get the number of entries
  tmp = bufTok.iterFirst(&i_state);
  while (tmp != NULL) {

    line_num++;

    // skip all blank spaces at beginning of line
    while (*tmp && isspace(*tmp)) {
      tmp++;
    }

    if (*tmp != '#' && *tmp != '\0') {

      current = (matcher_line *) xmalloc(sizeof(matcher_line));
      errPtr = parseConfigLine((char *) tmp, current, config_tags);

      if (errPtr != NULL) {
        snprintf(errBuf, sizeof(errBuf), "%s discarding %s entry at line %d : %s",
                 matcher_name, config_file_path, line_num, errPtr);
        IOCORE_SignalError(errBuf, alarmAlready);
        xfree(current);
      } else {

        // Line parsed ok.  Figure out what the destination
        //  type is and link it into our list
        numEntries++;
        current->line_num = line_num;

        switch (current->type) {
        case MATCH_HOST:
        case MATCH_DOMAIN:
          hostDomain++;
          break;
        case MATCH_NONE:
        default:
          ink_assert(0);
        }

        if (first == NULL) {
          ink_assert(last == NULL);
          first = last = current;
        } else {
          last->next = current;
          last = current;
        }
      }
    }
    tmp = bufTok.iterNext(&i_state);
  }

  // Make we have something to do before going on
  if (numEntries == 0) {
    /* no hosting customers -- put all the partitions in the
       generic table */

    if (gen_host_rec.Init(type)) {
      Warning("Problems encountered while initializing the Generic Partition");
    }

    if (first != NULL) {
      xfree(first);
    }
    return 0;
  }

  if (hostDomain > 0) {
    hostMatch = NEW(new CacheHostMatcher(matcher_name, config_file_path, type));
    hostMatch->AllocateSpace(hostDomain);
  }
  // Traverse the list and build the records table
  int generic_rec_initd = 0;
  current = first;
  while (current != NULL) {
    second_pass++;
    if ((current->type == MATCH_DOMAIN) || (current->type == MATCH_HOST)) {

      char *match_data = current->line[1][current->dest_entry];
      ink_assert(match_data != NULL);

      if (!strcasecmp(match_data, "*")) {
        // generic partitition - initialize the generic hostrecord */
        // Make sure that the line_info is not bogus
        ink_assert(current->dest_entry < MATCHER_MAX_TOKENS);

        // Remove our consumed label from the parsed line
        if (current->dest_entry < MATCHER_MAX_TOKENS)
          current->line[0][current->dest_entry] = NULL;
        else
          Warning("Problems encountered while initializing the Generic Partition");

        current->num_el--;
        if (!gen_host_rec.Init(current, type))
          generic_rec_initd = 1;
        else
          Warning("Problems encountered while initializing the Generic Partition");

      } else {
        hostMatch->NewEntry(current);
      }
    } else {
      snprintf(errBuf, sizeof(errBuf), "%s discarding %s entry with unknown type at line %d",
               matcher_name, config_file_path, current->line_num);
      IOCORE_SignalError(errBuf, alarmAlready);
    }

    // Deallocate the parsing structure
    last = current;
    current = current->next;
    xfree(last);
  }

  if (!generic_rec_initd) {
    const char *cache_type = (type == CACHE_HTTP_TYPE) ? "http" : "mixt";
    snprintf(errBuf, sizeof(errBuf),
             "No Partitions specified for Generic Hostnames for %s documents: %s cache will be disabled", cache_type,
             cache_type);
    IOCORE_SignalError(errBuf, alarmAlready);
  }

  ink_assert(second_pass == numEntries);

  if (is_debug_tag_set("matcher")) {
    Print();
  }
  return numEntries;
}

int
CacheHostTable::BuildTable()
{

  // File I/O Locals
  char *file_buf;
  int ret;

  file_buf = readIntoBuffer(config_file_path, matcher_name, NULL);

  if (file_buf == NULL) {
    Warning("Cannot read the config file: %s", config_file_path);
    gen_host_rec.Init(type);
    return 0;
  }

  ret = BuildTableFromString(file_buf);
  xfree(file_buf);
  return ret;
}

int
CacheHostRecord::Init(int typ)
{

  int i, j;
  extern Queue<CachePart> cp_list;
  extern int cp_list_len;
  char err[1024];
  err[0] = 0;
  num_part = 0;
  type = typ;
  cp = (CachePart **) xmalloc(cp_list_len * sizeof(CachePart *));
  memset(cp, 0, cp_list_len * sizeof(CachePart *));
  num_cachepart = 0;
  CachePart *cachep = cp_list.head;
  for (; cachep; cachep = cachep->link.next) {
    if (cachep->scheme == type) {
      Debug("cache_hosting", "Host Record: %xd, Partition: %d, size: %u", this, cachep->part_number, cachep->size);
      cp[num_cachepart] = cachep;
      num_cachepart++;
      num_part += cachep->num_parts;
    }
  }
  if (!num_cachepart) {
    snprintf(err, 1024, "error: No partitions found for Cache Type %d\n", type);
    IOCORE_SignalError(err, alarmAlready);
    return -1;
  }
  parts = (Part **) xmalloc(num_part * sizeof(Part *));
  int counter = 0;
  for (i = 0; i < num_cachepart; i++) {
    CachePart *cachep1 = cp[i];
    for (j = 0; j < cachep1->num_parts; j++) {
      parts[counter++] = cachep1->parts[j];
    }
  }
  ink_assert(counter == num_part);

  build_part_hash_table(this);
  return 0;
}

int
CacheHostRecord::Init(matcher_line * line_info, int typ)
{
  int i, j;
  extern Queue<CachePart> cp_list;
  char err[1024];
  err[0] = 0;
  int is_part_present = 0;
  char config_file[PATH_NAME_MAX];
  IOCORE_ReadConfigString(config_file, "proxy.config.cache.hosting_filename", PATH_NAME_MAX);
  type = typ;
  for (i = 0; i < MATCHER_MAX_TOKENS; i++) {
    char *label = line_info->line[0][i];
    if (!label)
      continue;
    char *val;

    if (!strcasecmp(label, "partition")) {
      /* parse the list of partitions */
      val = strdup(line_info->line[1][i]);
      char *part_no = val;
      char *s = val;
      int partition_number;
      CachePart *cachep;
      /* first find out the number of partitions */
      while (*s) {
        if ((*s == ',')) {
          num_cachepart++;
          s++;
          if (!(*s)) {
            const char *errptr = "A partition number expected";
            snprintf(err, 1024,
                         "%s discarding %s entry at line %d :%s",
                         "[CacheHosting]", config_file, line_info->line_num, errptr);
            IOCORE_SignalError(err, alarmAlready);
            if (val != NULL) {
              xfree(val);
            }
            return -1;
          }
        }
        if ((*s<'0') || (*s> '9')) {
          snprintf(err, 1024,
                       "%s discarding %s entry at line %d : bad token [%c]",
                       "[CacheHosting]", config_file, line_info->line_num, *s);
          IOCORE_SignalError(err, alarmAlready);
          if (val != NULL) {
            xfree(val);
          }
          return -1;
        }
        s++;
      }
      s = val;
      num_cachepart++;
      cp = (CachePart **) xmalloc(num_cachepart * sizeof(CachePart *));
      memset(cp, 0, num_cachepart * sizeof(CachePart *));
      num_cachepart = 0;
      while (1) {
        char c = *s;
        if ((c == ',') || (c == '\0')) {
          *s = '\0';
          partition_number = atoi(part_no);

          cachep = cp_list.head;
          for (; cachep; cachep = cachep->link.next) {
            if (cachep->part_number == partition_number) {
              is_part_present = 1;
              if ((cachep->scheme == type)) {
                Debug("cache_hosting",
                      "Host Record: %xd, Partition: %d, size: %ld",
                      this, partition_number, cachep->size * STORE_BLOCK_SIZE);
                cp[num_cachepart] = cachep;
                num_cachepart++;
                num_part += cachep->num_parts;
                break;
              }
            }
          }
          if (!is_part_present) {
            snprintf(err, 1024,
                         "%s discarding %s entry at line %d : bad partition number [%d]",
                         "[CacheHosting]", config_file, line_info->line_num, partition_number);
            IOCORE_SignalError(err, alarmAlready);
            if (val != NULL) {
              xfree(val);
            }
            return -1;
          }
          if (c == '\0')
            break;
          part_no = s + 1;
        }
        s++;
      }
      if (val != NULL) {
        xfree(val);
      }
      break;
    }

    snprintf(err, 1024,
                 "%s discarding %s entry at line %d : bad token [%s]",
                 "[CacheHosting]", config_file, line_info->line_num, label);
    IOCORE_SignalError(err, alarmAlready);
    return -1;
  }

  if (i == MATCHER_MAX_TOKENS) {
    snprintf(err, 1024,
                 "%s discarding %s entry at line %d : No partitions specified",
                 "[CacheHosting]", config_file, line_info->line_num);
    IOCORE_SignalError(err, alarmAlready);
    return -1;
  }

  if (!num_part) {
    return -1;
  }
  parts = (Part **) xmalloc(num_part * sizeof(Part *));
  int counter = 0;
  for (i = 0; i < num_cachepart; i++) {
    CachePart *cachep = cp[i];
    for (j = 0; j < cp[i]->num_parts; j++) {
      parts[counter++] = cachep->parts[j];
    }
  }
  ink_assert(counter == num_part);

  build_part_hash_table(this);
  return 0;
}

void
CacheHostRecord::UpdateMatch(CacheHostResult * r, char *rd)
{
  NOWARN_UNUSED(rd);
  r->record = this;
}

void
CacheHostRecord::Print()
{
}



void
ConfigPartitions::read_config_file()
{

// File I/O Locals
  char *file_buf;
  char config_file_path[PATH_NAME_MAX];
  char *config_file = NULL;
  config_file_path[0] = '\0';

  IOCORE_ReadConfigStringAlloc(config_file, "proxy.config.cache.partition_filename");
  ink_release_assert(config_file != NULL);
  ink_strncpy(config_file_path, cache_system_config_directory, sizeof(config_file_path));
  strncat(config_file_path, DIR_SEP, (sizeof(config_file_path) - strlen(config_file_path) - 1));
  strncat(config_file_path, config_file, (sizeof(config_file_path) - strlen(config_file_path) - 1));
  xfree(config_file);

  file_buf = readIntoBuffer(config_file_path, "[CachePartition]", NULL);

  if (file_buf == NULL) {
    Warning("Cannot read the config file: %s", config_file_path);
    return;
  }

  BuildListFromString(config_file_path, file_buf);
  xfree(file_buf);
  return;
}

void
ConfigPartitions::BuildListFromString(char *config_file_path, char *file_buf)
{

#define PAIR_ZERO 0
#define PAIR_ONE 1
#define PAIR_TWO 2
#define DONE 3
#define INK_ERROR -1

#define INK_ERROR_PARTITION -2  //added by YTS Team, yamsat for bug id 59632
// Table build locals
  Tokenizer bufTok("\n");
  tok_iter_state i_state;
  const char *tmp;
  char *end;
  char *line_end = NULL;
  int line_num = 0;
  int total = 0;                //added by YTS Team, yamsat for bug id 59632
  char errBuf[1024];

  char partition_seen[256];
  int state = 0;                //changed by YTS Team, yamsat for bug id 59632
  int manager_alarmed = false;
  int partition_number = 0;
  int scheme = CACHE_NONE_TYPE;
  int size = 0;
  int in_percent = 0;
  const char *matcher_name = "[CachePartition]";

  memset(partition_seen, 0, sizeof(partition_seen));
  num_partitions = 0;
  num_stream_partitions = 0;
  num_http_partitions = 0;

  if (bufTok.Initialize(file_buf, SHARE_TOKS | ALLOW_EMPTY_TOKS) == 0) {
    // We have an empty file
    /* no partitions */
    return;
  }
  // First get the number of entries
  tmp = bufTok.iterFirst(&i_state);
  while (tmp != NULL) {
    state = PAIR_ZERO;
    line_num++;

    // skip all blank spaces at beginning of line
    while (1) {
      while (*tmp && isspace(*tmp)) {
        tmp++;
      }

      if (!(*tmp) && state == DONE) {
        /* add the config */

        ConfigPart *configp = NEW(new ConfigPart());
        configp->number = partition_number;
        if (in_percent) {
          configp->percent = size;
          configp->in_percent = 1;
        } else {
          configp->in_percent = 0;
        }
        configp->scheme = scheme;
        configp->size = size;
        configp->cachep = NULL;
        cp_queue.enqueue(configp);
        num_partitions++;
        if (scheme == CACHE_HTTP_TYPE)
          num_http_partitions++;
        else
          num_stream_partitions++;
        Debug("cache_hosting",
              "added partition=%d, scheme=%d, size=%d percent=%d\n", partition_number, scheme, size, in_percent);
        break;
      }

      if (state == PAIR_ZERO) {
        if (*tmp == '\0' || *tmp == '#')
          break;
      } else {
        if (!(*tmp)) {
          snprintf(errBuf, sizeof(errBuf), "%s discarding %s entry at line %d : Unexpected end of line",
                   matcher_name, config_file_path, line_num);
          IOCORE_SignalError(errBuf, manager_alarmed);

          break;
        }
      }

      end = (char *) tmp;
      while (*end && !isspace(*end))
        end++;

      if (!(*end))
        line_end = end;
      else {
        line_end = end + 1;
        *end = '\0';
      }
      char *eq_sign;

      eq_sign = (char *) strchr(tmp, '=');
      if (!eq_sign) {
        state = INK_ERROR;
      } else
        *eq_sign = '\0';

      switch (state) {
      case PAIR_ZERO:
        if (strcasecmp(tmp, "partition")) {
          state = INK_ERROR;
          break;
        }
        tmp += 10;              //size of string partition including null
        partition_number = atoi(tmp);

        // XXX should this be < 0 instead of < 1
        if (partition_number<1 || partition_number> 255 || partition_seen[partition_number]) {

          const char *err;

          // XXX should this be < 0 instead of < 1
          if (partition_number<1 || partition_number> 255) {
            err = "Bad Partition Number";
          } else {
            err = "Partition Already Specified";
          }

          snprintf(errBuf, sizeof(errBuf), "%s discarding %s entry at line %d : %s [%d]",
                   matcher_name, config_file_path, line_num, err, partition_number);
          IOCORE_SignalError(errBuf, manager_alarmed);
          state = INK_ERROR;
          break;
        }

        partition_seen[partition_number] = 1;
        while (ParseRules::is_digit(*tmp))
          tmp++;
        state = PAIR_ONE;
        break;

      case PAIR_ONE:
        if (strcasecmp(tmp, "scheme")) {
          state = INK_ERROR;
          break;
        }
        tmp += 7;               //size of string scheme including null

        if (!strcasecmp(tmp, "http")) {
          tmp += 4;
          scheme = CACHE_HTTP_TYPE;
        } else if (!strcasecmp(tmp, "mixt")) {
          tmp += 4;
          scheme = CACHE_RTSP_TYPE;
        } else {
          state = INK_ERROR;
          break;
        }

        state = PAIR_TWO;
        break;

      case PAIR_TWO:

        if (strcasecmp(tmp, "size")) {
          state = INK_ERROR;
          break;
        }
        tmp += 5;
        size = atoi(tmp);

        while (ParseRules::is_digit(*tmp))
          tmp++;

        if (*tmp == '%') {
          //added by YTS Team, yamsat for bug id 59632
          total += size;
          if (size > 100 || total > 100) {
            state = INK_ERROR_PARTITION;
            if (state == INK_ERROR_PARTITION || *tmp) {
              snprintf(errBuf, sizeof(errBuf),
                       "Total partition size added upto more than 100 percent,No partitions created");
              IOCORE_SignalError(errBuf, manager_alarmed);
              break;
            }
          }
          //ends here
          in_percent = 1;
          tmp++;
        } else
          in_percent = 0;
        state = DONE;
        break;

      }

      if (state == INK_ERROR || *tmp) {
        snprintf(errBuf, sizeof(errBuf), "%s discarding %s entry at line %d : Invalid token [%s]",
                 matcher_name, config_file_path, line_num, tmp);
        IOCORE_SignalError(errBuf, manager_alarmed);

        break;
      }
      //added by YTS Team, yamsat for bug id 59632
      if (state == INK_ERROR_PARTITION || *tmp) {
        snprintf(errBuf, sizeof(errBuf), "Total partition size added upto more than 100 percent,No partitions created");
        IOCORE_SignalError(errBuf, manager_alarmed);
        break;
      }
      // ends here
      if (end < line_end)
        tmp++;
    }
    tmp = bufTok.iterNext(&i_state);
  }

  return;
}



/* Test the cache partitioning with different configurations */
#define MEGS_128 (128 * 1024 * 1024)
#define ROUND_TO_PART_SIZE(_x) (((_x) + (MEGS_128 - 1)) &~ (MEGS_128 - 1))
extern CacheDisk **gdisks;
extern Queue<CachePart> cp_list;
extern int cp_list_len;
extern ConfigPartitions config_partitions;
extern volatile int gnpart;

extern void cplist_init();
extern int cplist_reconfigure();
static int configs = 4;

Queue<CachePart> saved_cp_list;
int saved_cp_list_len;
ConfigPartitions saved_config_partitions;
int saved_gnpart;

int ClearConfigPart(ConfigPartitions * configp);
int ClearCachePartList(Queue<CachePart> *cpl, int len);
int create_config(RegressionTest * t, int i);
int execute_and_verify(RegressionTest * t);
void save_state();
void restore_state();

EXCLUSIVE_REGRESSION_TEST(Cache_part) (RegressionTest * t, int atype, int *status) {
  NOWARN_UNUSED(atype);
  save_state();
  srand48(time(NULL));
  *status = REGRESSION_TEST_PASSED;
  for (int i = 0; i < configs; i++) {
    if (create_config(t, i)) {
      if (execute_and_verify(t) == REGRESSION_TEST_FAILED) {
        *status = REGRESSION_TEST_FAILED;
      }
    }
  }
  restore_state();
  return;
}

int
create_config(RegressionTest * t, int num)
{


  int i = 0;
  int part_num = 1;
  // clear all old configurations before adding new test cases
  config_partitions.clear_all();
  switch (num) {
  case 0:
    for (i = 0; i < gndisks; i++) {
      CacheDisk *d = gdisks[i];
      int blocks = d->num_usable_blocks;
      if (blocks < STORE_BLOCKS_PER_PART) {
        rprintf(t, "Cannot run Cache_part regression: not enough disk space\n");
        return 0;
      }
      /* create 128 MB partitions */
      for (; blocks >= STORE_BLOCKS_PER_PART; blocks -= STORE_BLOCKS_PER_PART) {
        if (part_num > 255)
          break;
        ConfigPart *cp = NEW(new ConfigPart());
        cp->number = part_num++;
        cp->scheme = CACHE_HTTP_TYPE;
        cp->size = 128;
        cp->in_percent = 0;
        cp->cachep = 0;
        config_partitions.cp_queue.enqueue(cp);
        config_partitions.num_partitions++;
        config_partitions.num_http_partitions++;
      }

    }
    rprintf(t, "%d 128 Megabyte Partitions\n", part_num - 1);

    break;

  case 1:
    {
      for (i = 0; i < gndisks; i++) {
        gdisks[i]->delete_all_partitions();
      }

      // calculate the total free space
      inku64 total_space = 0;
      for (i = 0; i < gndisks; i++) {
        int part_blocks = gdisks[i]->num_usable_blocks;
        /* round down the blocks to the nearest
           multiple of STORE_BLOCKS_PER_PART */
        part_blocks = (part_blocks / STORE_BLOCKS_PER_PART)
          * STORE_BLOCKS_PER_PART;
        total_space += part_blocks;
      }

      // make sure we have atleast 1280 M bytes
      if (total_space<(10 << 27)>> STORE_BLOCK_SHIFT) {
        rprintf(t, "Not enough space for 10 partition\n");
        return 0;
      }

      part_num = 1;
      rprintf(t, "Cleared  disk\n");
      for (i = 0; i < 10; i++) {
        ConfigPart *cp = NEW(new ConfigPart());
        cp->number = part_num++;
        cp->scheme = CACHE_HTTP_TYPE;
        cp->size = 10;
        cp->percent = 10;
        cp->in_percent = 1;
        cp->cachep = 0;
        config_partitions.cp_queue.enqueue(cp);
        config_partitions.num_partitions++;
        config_partitions.num_http_partitions++;
      }
      rprintf(t, "10 partition, 10 percent each\n");
    }
    break;

  case 2:
  case 3:

    {
      /* calculate the total disk space */
      InkRand *gen = &this_ethread()->generator;
      inku64 total_space = 0;
      part_num = 1;
      if (num == 2) {
        rprintf(t, "Random Partitions after clearing the disks\n");
      } else {
        rprintf(t, "Random Partitions without clearing the disks\n");
      }

      for (i = 0; i < gndisks; i++) {
        int part_blocks = gdisks[i]->num_usable_blocks;
        /* round down the blocks to the nearest
           multiple of STORE_BLOCKS_PER_PART */
        part_blocks = (part_blocks / STORE_BLOCKS_PER_PART)
          * STORE_BLOCKS_PER_PART;
        total_space += part_blocks;

        if (num == 2) {
          gdisks[i]->delete_all_partitions();
        } else {
          gdisks[i]->cleared = 0;
        }
      }
      while (total_space > 0) {
        if (part_num > 255)
          break;
        ink_off_t modu = MAX_PART_SIZE;
        if (total_space<(MAX_PART_SIZE>> STORE_BLOCK_SHIFT)) {
          modu = total_space * STORE_BLOCK_SIZE;
        }

        ink_off_t random_size = (gen->random() % modu) + 1;
        /* convert to 128 megs multiple */
        int scheme = (random_size % 2) ? CACHE_HTTP_TYPE : CACHE_RTSP_TYPE;
        random_size = ROUND_TO_PART_SIZE(random_size);
        int blocks = random_size / STORE_BLOCK_SIZE;
        ink_assert(blocks <= (int) total_space);
        total_space -= blocks;

        ConfigPart *cp = NEW(new ConfigPart());

        cp->number = part_num++;
        cp->scheme = scheme;
        cp->size = random_size >> 20;
        cp->percent = 0;
        cp->in_percent = 0;
        cp->cachep = 0;
        config_partitions.cp_queue.enqueue(cp);
        config_partitions.num_partitions++;
        if (cp->scheme == CACHE_HTTP_TYPE) {
          config_partitions.num_http_partitions++;
          rprintf(t, "partition=%d scheme=http size=%d\n", cp->number, cp->size);
        } else {
          config_partitions.num_stream_partitions++;
          rprintf(t, "partition=%d scheme=rtsp size=%d\n", cp->number, cp->size);

        }
      }
    }
    break;

  default:
    return 1;
  }
  return 1;
}

int
execute_and_verify(RegressionTest * t)
{
  int i;
  cplist_init();
  cplist_reconfigure();

  /* compare the partitions */
  if (cp_list_len != config_partitions.num_partitions)
    return REGRESSION_TEST_FAILED;

  /* check that the partitions and sizes
     match the configuration */
  int matched = 0;
  ConfigPart *cp = config_partitions.cp_queue.head;
  CachePart *cachep;

  for (i = 0; i < config_partitions.num_partitions; i++) {
    cachep = cp_list.head;
    while (cachep) {
      if (cachep->part_number == cp->number) {
        if ((cachep->scheme != cp->scheme) ||
            (cachep->size != (cp->size << (20 - STORE_BLOCK_SHIFT))) || (cachep != cp->cachep)) {
          rprintf(t, "Configuration and Actual partitions don't match\n");
          return REGRESSION_TEST_FAILED;
        }

        /* check that the number of partitions match the ones
           on disk */
        int d_no;
        int m_parts = 0;
        for (d_no = 0; d_no < gndisks; d_no++) {
          if (cachep->disk_parts[d_no]) {
            DiskPart *dp = cachep->disk_parts[d_no];
            if (dp->part_number != cachep->part_number) {
              rprintf(t, "DiskParts and CacheParts don't match\n");
              return REGRESSION_TEST_FAILED;
            }

            /* check the diskpartblock queue */
            DiskPartBlockQueue *dpbq = dp->dpb_queue.head;
            while (dpbq) {
              if (dpbq->b->number != cachep->part_number) {
                rprintf(t, "DiskPart and DiskPartBlocks don't match\n");
                return REGRESSION_TEST_FAILED;
              }
              dpbq = dpbq->link.next;
            }

            m_parts += dp->num_partblocks;
          }
        }
        if (m_parts != cachep->num_parts) {
          rprintf(t, "Num partitions in CachePart and DiskPart don't match\n");
          return REGRESSION_TEST_FAILED;
        }
        matched++;
        break;
      }
      cachep = cachep->link.next;
    }
  }

  if (matched != config_partitions.num_partitions) {
    rprintf(t, "Num of Partitions created and configured don't match\n");
    return REGRESSION_TEST_FAILED;
  }

  ClearConfigPart(&config_partitions);

  ClearCachePartList(&cp_list, cp_list_len);

  for (i = 0; i < gndisks; i++) {
    CacheDisk *d = gdisks[i];
    if (is_debug_tag_set("cache_hosting")) {
      int j;

      Debug("cache_hosting", "Disk: %d: Part Blocks: %ld: Free space: %ld",
            i, d->header->num_diskpart_blks, d->free_space);
      for (j = 0; j < (int) d->header->num_partitions; j++) {

        Debug("cache_hosting", "\tPart: %d Size: %d", d->disk_parts[j]->part_number, d->disk_parts[j]->size);
      }
      for (j = 0; j < (int) d->header->num_diskpart_blks; j++) {
        Debug("cache_hosting", "\tBlock No: %d Size: %d Free: %d",
              d->header->part_info[j].number, d->header->part_info[j].len, d->header->part_info[j].free);
      }
    }
  }
  return REGRESSION_TEST_PASSED;
}

int
ClearConfigPart(ConfigPartitions * configp)
{

  int i = 0;
  ConfigPart *cp = NULL;
  while ((cp = configp->cp_queue.dequeue())) {
    delete cp;
    i++;
  }
  if (i != configp->num_partitions) {
    Warning("failed");
    return 0;
  }
  configp->num_partitions = 0;
  configp->num_http_partitions = 0;
  configp->num_stream_partitions = 0;
  return 1;
}

int
ClearCachePartList(Queue<CachePart> *cpl, int len)
{

  int i = 0;
  CachePart *cp = NULL;
  while ((cp = cpl->dequeue())) {
    if (cp->disk_parts)
      xfree(cp->disk_parts);
    if (cp->parts)
      xfree(cp->parts);
    delete(cp);
    i++;
  }

  if (i != len) {
    Warning("Failed");
    return 0;
  }
  return 1;
}


void
save_state()
{
  saved_cp_list = cp_list;
  saved_cp_list_len = cp_list_len;
  memcpy(&saved_config_partitions, &config_partitions, sizeof(ConfigPartitions));
  saved_gnpart = gnpart;
  memset(&cp_list, 0, sizeof(Queue<CachePart>));
  memset(&config_partitions, 0, sizeof(ConfigPartitions));
  gnpart = 0;
}

void
restore_state()
{

  cp_list = saved_cp_list;
  cp_list_len = saved_cp_list_len;
  memcpy(&config_partitions, &saved_config_partitions, sizeof(ConfigPartitions));
  gnpart = saved_gnpart;
}
