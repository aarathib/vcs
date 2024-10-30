#include <iostream>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <openssl/sha.h>
#include "zstr.hpp"

#define BUFFER_SIZE 64 * 1024

using namespace std;
using namespace std::filesystem;

struct TreeEntry
{
    string filemod;
    string filename;
    string filetype;
    string filehash;
};

struct User
{
    string name;
    string email;
};

User user = {"aarathib", "aarathy00@gmail.com"};

struct Metadata
{
    string sha;
    string filemod;
};

vector<char> compressData(const string &data)
{
    vector<char> compressedData(compressBound(data.size()));
    uLongf compressedSize = compressedData.size();

    if (compress(reinterpret_cast<Bytef *>(compressedData.data()), &compressedSize,
                 reinterpret_cast<const Bytef *>(data.data()), data.size()) != Z_OK)
    {
        cerr << "ERR: Compression failed";
    }

    compressedData.resize(compressedSize);
    return compressedData;
}

string hashObject(string filepath, bool create_blob, bool printsha = false)
{
    SHA_CTX shaContext;
    SHA1_Init(&shaContext);

    ifstream ipStream(filepath, ios::binary);
    if (!ipStream)
    {
        cerr << "Err: Cannot open file " << filepath << "\n";
        return "";
    }

    ipStream.seekg(0, ios::end);
    size_t filesize = ipStream.tellg();
    ipStream.seekg(0, ios::beg);
    string header = "blob " + to_string(filesize) + '\0';
    SHA1_Update(&shaContext, header.c_str(), header.size());

    char buffer[BUFFER_SIZE];
    while (true)
    {
        ipStream.read(buffer, BUFFER_SIZE);
        streamsize bytesRead = ipStream.gcount();
        if (bytesRead == 0)
            break;

        SHA1_Update(&shaContext, buffer, bytesRead);
    }

    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1_Final(hash, &shaContext);
    stringstream ss;
    for (int i = 0; i < SHA_DIGEST_LENGTH; i++)
    {
        ss << hex << setw(2) << setfill('0') << static_cast<int>(hash[i]);
    }
    string sha = ss.str();
    if (printsha)
        cout << "sha1 " << sha << '\n';

    if (create_blob)
    {
        ipStream.clear();
        ipStream.seekg(0, ios::beg);

        z_stream deflateStream;
        deflateStream.zalloc = Z_NULL;
        deflateStream.zfree = Z_NULL;
        deflateStream.opaque = Z_NULL;

        if (deflateInit(&deflateStream, Z_BEST_COMPRESSION) != Z_OK)
        {
            cerr << "ERR: Could not initialize compression stream\n";
            return "";
        }

        string subdir = sha.substr(0, 2);
        string filename = sha.substr(2);
        create_directories(".mygit/objects/" + subdir);
        string blobpath = ".mygit/objects/" + subdir + "/" + filename;

        ofstream outFile(blobpath, ios::binary);
        if (!outFile)
        {
            cerr << "ERR: Cannot create blob file\n";
            deflateEnd(&deflateStream);
            return "";
        }

        deflateStream.avail_in = header.size();
        deflateStream.next_in = reinterpret_cast<Bytef *>(header.data());
        char compressBuffer[BUFFER_SIZE];

        do
        {
            deflateStream.avail_out = BUFFER_SIZE;
            deflateStream.next_out = reinterpret_cast<Bytef *>(compressBuffer);
            deflate(&deflateStream, Z_NO_FLUSH);
            outFile.write(compressBuffer, BUFFER_SIZE - deflateStream.avail_out);
        } while (deflateStream.avail_out == 0);

        while (true)
        {
            ipStream.read(buffer, BUFFER_SIZE);
            streamsize bytesRead = ipStream.gcount();
            if (bytesRead == 0)
                break;

            deflateStream.avail_in = bytesRead;
            deflateStream.next_in = reinterpret_cast<Bytef *>(buffer);

            do
            {
                deflateStream.avail_out = BUFFER_SIZE;
                deflateStream.next_out = reinterpret_cast<Bytef *>(compressBuffer);
                deflate(&deflateStream, Z_NO_FLUSH);
                outFile.write(compressBuffer, BUFFER_SIZE - deflateStream.avail_out);
            } while (deflateStream.avail_out == 0);
        }

        // Finish the compression
        deflateStream.avail_in = 0;
        deflateStream.next_in = nullptr;
        do
        {
            deflateStream.avail_out = BUFFER_SIZE;
            deflateStream.next_out = reinterpret_cast<Bytef *>(compressBuffer);

            deflate(&deflateStream, Z_FINISH);
            outFile.write(compressBuffer, BUFFER_SIZE - deflateStream.avail_out);
        } while (deflateStream.avail_out == 0);

        deflateEnd(&deflateStream);
        outFile.close();
        if (printsha)
            cout << "Compressed and stored object at " << blobpath << '\n';
    }

    return sha;
}

int catFile(string &flag, string &fileSha)
{
    string dirname = fileSha.substr(0, 2);
    string filename = fileSha.substr(2);
    string objPath = ".mygit/objects/" + dirname + '/' + filename;

    ifstream ipStream(objPath, ios::binary);
    if (!ipStream)
    {
        cerr << "ERR: Cannot open file " << objPath << "\n";
        return 1;
    }

    z_stream inflateStream;
    inflateStream.zalloc = Z_NULL;
    inflateStream.zfree = Z_NULL;
    inflateStream.opaque = Z_NULL;

    if (inflateInit(&inflateStream) != Z_OK)
    {
        cerr << "ERR: Could not initialize decompression stream\n";
        return 1;
    }

    char buffer[BUFFER_SIZE];
    char decompressBuffer[BUFFER_SIZE];

    ipStream.read(buffer, BUFFER_SIZE);
    inflateStream.avail_in = ipStream.gcount();
    inflateStream.next_in = reinterpret_cast<Bytef *>(buffer);

    string header;
    do
    {
        inflateStream.avail_out = BUFFER_SIZE;
        inflateStream.next_out = reinterpret_cast<Bytef *>(decompressBuffer);

        int ret = inflate(&inflateStream, Z_NO_FLUSH);
        if (ret == Z_STREAM_ERROR || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR)
        {
            cerr << "ERR: Decompression error\n";
            inflateEnd(&inflateStream);
            return 1;
        }

        header.append(decompressBuffer, BUFFER_SIZE - inflateStream.avail_out);

    } while (header.find('\0') == string::npos);

    size_t nullPos = header.find('\0');
    string typeAndSize = header.substr(0, nullPos);
    istringstream iss(typeAndSize);
    string type;
    size_t size;
    iss >> type >> size;
    if (flag == "-t")
    {
        cout << type << '\n';
    }
    else if (flag == "-s")
    {
        cout << size << '\n';
    }
    else if (flag == "-p")
    {
        cout.write(header.data() + nullPos + 1, header.size() - (nullPos + 1));
        while (ipStream)
        {
            ipStream.read(buffer, BUFFER_SIZE);
            streamsize bytesRead = ipStream.gcount();
            if (bytesRead == 0)
                break;

            inflateStream.avail_in = bytesRead;
            inflateStream.next_in = reinterpret_cast<Bytef *>(buffer);

            do
            {
                inflateStream.avail_out = BUFFER_SIZE;
                inflateStream.next_out = reinterpret_cast<Bytef *>(decompressBuffer);

                int ret = inflate(&inflateStream, Z_NO_FLUSH);
                if (ret == Z_STREAM_ERROR || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR)
                {
                    cerr << "ERR: Decompression error\n";
                    inflateEnd(&inflateStream);
                    return 1;
                }
                cout.write(decompressBuffer, BUFFER_SIZE - inflateStream.avail_out);
            } while (inflateStream.avail_out == 0);
        }
        cout << '\n';
    }
    else
    {
        cerr << "ERR: Invalid flag\n";
        inflateEnd(&inflateStream);
        ipStream.close();
        return 1;
    }

    inflateEnd(&inflateStream);
    ipStream.close();
    return 0;
}

string hashBlob(string &text)
{
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char *>(text.c_str()), text.size(), hash);

    string hashStr;
    for (int i = 0; i < SHA_DIGEST_LENGTH; i++)
    {
        char buffer[3];
        sprintf(buffer, "%02x", hash[i]);
        hashStr += buffer;
    }

    return hashStr;
}

string writeBlob(path &filepath)
{
    SHA_CTX shaContext;
    SHA1_Init(&shaContext);

    ifstream ipStream(filepath, ios::binary);
    if (!ipStream)
    {
        cerr << "ERR: Cannot open file " << filepath << "\n";
        return "";
    }

    char buffer[BUFFER_SIZE];
    while (true)
    {
        ipStream.read(buffer, BUFFER_SIZE);
        streamsize bytesRead = ipStream.gcount();
        if (bytesRead == 0)
            break;

        SHA1_Update(&shaContext, buffer, bytesRead);
    }

    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1_Final(hash, &shaContext);
    stringstream ss;
    for (int i = 0; i < SHA_DIGEST_LENGTH; i++)
    {
        ss << hex << setw(2) << setfill('0') << static_cast<int>(hash[i]);
    }
    string sha = ss.str();
    // cout << "sha1 " << sha << " " << filepath << '\n';
    return sha;
}

int writeObject(string &object)
{
    string hash = hashBlob(object);
    // cout << "tree hash: " << hash << " dirpath: " << dirpath << '\n';
    string objPath = ".mygit/objects/" + hash.substr(0, 2);
    create_directories(objPath);

    objPath += "/" + hash.substr(2);
    ofstream opFile(objPath, ios::binary);
    string header = "tree " + to_string(object.size()) + '\0';
    string fullobj = header + object;

    z_stream deflateStream;
    deflateStream.zalloc = Z_NULL;
    deflateStream.zfree = Z_NULL;
    deflateStream.opaque = Z_NULL;

    if (deflateInit(&deflateStream, Z_BEST_COMPRESSION) != Z_OK)
    {
        cerr << "ERR: Could not initialize compression stream\n";
        return 1;
    }

    deflateStream.avail_in = fullobj.size();
    deflateStream.next_in = reinterpret_cast<Bytef *>(fullobj.data());
    char compressBuffer[BUFFER_SIZE];

    do
    {
        deflateStream.avail_out = BUFFER_SIZE;
        deflateStream.next_out = reinterpret_cast<Bytef *>(compressBuffer);
        deflate(&deflateStream, Z_NO_FLUSH);
        opFile.write(compressBuffer, BUFFER_SIZE - deflateStream.avail_out);
    } while (deflateStream.avail_out == 0);

    deflateStream.avail_in = 0;
    deflateStream.next_in = nullptr;
    do
    {
        deflateStream.avail_out = BUFFER_SIZE;
        deflateStream.next_out = reinterpret_cast<Bytef *>(compressBuffer);

        deflate(&deflateStream, Z_FINISH);
        opFile.write(compressBuffer, BUFFER_SIZE - deflateStream.avail_out);
    } while (deflateStream.avail_out == 0);

    deflateEnd(&deflateStream);
    opFile.close();

    return 0;
}

string writeTree(path directoryPath)
{
    vector<string> entries;

    for (auto &entry : directory_iterator(directoryPath))
    {
        if (entry.is_regular_file())
        {
            path filepath = entry.path();
            string blobHash = writeBlob(filepath);
            string entryData = "100644 " + entry.path().filename().string() + '\0' + blobHash;
            entries.push_back(entryData);

            hashObject(filepath, true);
        }
        else if (entry.is_directory())
        {
            if (entry.path().filename() == ".mygit" || entry.path().filename() == ".git")
                continue;

            path dirpath = entry.path();
            string treeHash = writeTree(dirpath);
            string entryData = "040000 " + entry.path().filename().string() + '\0' + treeHash;
            entries.push_back(entryData);
        }
    }

    string treeData;
    for (const auto &entry : entries)
    {
        treeData += entry;
    }

    writeObject(treeData);
    return hashBlob(treeData);
}

string decompressData(const string &compressedData)
{
    string decompressedData;
    z_stream inflateStream;
    inflateStream.zalloc = Z_NULL;
    inflateStream.zfree = Z_NULL;
    inflateStream.opaque = Z_NULL;
    inflateStream.avail_in = compressedData.size();
    inflateStream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(compressedData.data()));

    if (inflateInit(&inflateStream) != Z_OK)
    {
        cerr << "ERR: Could not initialize decompression stream\n";
        return "";
    }

    char outBuffer[BUFFER_SIZE];
    int ret;

    do
    {
        inflateStream.avail_out = BUFFER_SIZE;
        inflateStream.next_out = reinterpret_cast<Bytef *>(outBuffer);

        ret = inflate(&inflateStream, Z_NO_FLUSH);
        if (ret == Z_STREAM_ERROR || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR)
        {
            cerr << "ERR: Decompression error\n";
            inflateEnd(&inflateStream);
            return "";
        }

        decompressedData.append(outBuffer, BUFFER_SIZE - inflateStream.avail_out);

    } while (inflateStream.avail_out == 0);

    ret = inflate(&inflateStream, Z_FINISH);
    if (ret != Z_STREAM_END)
    {
        cerr << "ERR: Unexpected end of decompression\n";
        inflateEnd(&inflateStream);
        return "";
    }

    inflateEnd(&inflateStream);
    return decompressedData;
}
unordered_map<string, TreeEntry> parseTreeDataMap(string treeData)
{
    unordered_map<string, TreeEntry> newTreeMap;
    ssize_t pos = 0;
    size_t stringEnd = treeData.find('\0', pos);
    stringstream ss;
    ss << treeData.substr(0, stringEnd);
    string objtype, objsize;
    ss >> objtype >> objsize;
    ss.clear();
    if (objtype != "tree")
    {
        cout << "Not a tree object\n";
        return newTreeMap;
    }

    pos = stringEnd + 1;
    while (pos < treeData.size())
    {
        TreeEntry entry;
        stringEnd = treeData.find('\0', pos);
        ss << treeData.substr(pos, stringEnd - pos);
        ss >> entry.filemod >> entry.filename;
        ss.clear();
        pos = stringEnd + 1;

        entry.filehash.clear();
        entry.filehash = treeData.substr(pos, 40);
        pos += 40;
        entry.filetype = (entry.filemod == "100644" ? "blob" : "tree");
        newTreeMap[entry.filename] = entry;
    }
    return newTreeMap;
}

vector<TreeEntry> parseTreeData(string treeData)
{
    vector<TreeEntry> entries;
    ssize_t pos = 0;
    size_t stringEnd = treeData.find('\0', pos);
    stringstream ss;
    ss << treeData.substr(0, stringEnd);
    string objtype, objsize;
    ss >> objtype >> objsize;
    ss.clear();
    if (objtype != "tree")
    {
        cout << "Not a tree object\n";
        return entries;
    }

    pos = stringEnd + 1;
    while (pos < treeData.size())
    {
        TreeEntry entry;
        stringEnd = treeData.find('\0', pos);
        ss << treeData.substr(pos, stringEnd - pos);
        ss >> entry.filemod >> entry.filename;
        ss.clear();
        pos = stringEnd + 1;

        entry.filehash.clear();
        entry.filehash = treeData.substr(pos, 40);
        pos += 40;
        entry.filetype = (entry.filemod == "100644" ? "blob" : "tree");
        entries.push_back(entry);
    }
    return entries;
}

void displayTree(vector<TreeEntry> &entries, bool nameonly)
{
    if (nameonly)
    {
        for (TreeEntry entry : entries)
        {
            cout << entry.filename;
            if (entry.filetype == "tree")
                cout << "/";
            cout << '\n';
        }
    }
    else
    {
        for (TreeEntry entry : entries)
        {
            cout << entry.filemod << "  " << entry.filetype << "  " << entry.filehash << "  " << entry.filename << '\n';
        }
    }
}

unordered_map<string, TreeEntry> getTreeData(string tree_sha)
{
    string dir = tree_sha.substr(0, 2);
    string file = tree_sha.substr(2);
    string objPath = ".mygit/objects/" + dir + '/' + file;
    ifstream inFile(objPath, ios::binary);
    if (!inFile)
    {
        cerr << "Tree object not found\n";
        return {};
    }

    stringstream buffer;
    buffer << inFile.rdbuf();
    string compressedData = buffer.str();

    string treeData = decompressData(compressedData);
    if (treeData.empty())
    {
        cerr << "ERR: Failed to decompress tree object\n";
        return {};
    }

    return parseTreeDataMap(treeData);
}

int lstree(string tree_sha, bool nameonly)
{
    string dir = tree_sha.substr(0, 2);
    string file = tree_sha.substr(2);
    string objPath = ".mygit/objects/" + dir + '/' + file;
    ifstream inFile(objPath, ios::binary);
    if (!inFile)
    {
        cerr << "Tree object not found\n";
        return 1;
    }

    stringstream buffer;
    buffer << inFile.rdbuf();
    string compressedData = buffer.str();

    string treeData = decompressData(compressedData);
    cout << "treedata " << treeData << '\n';
    if (treeData.empty())
    {
        cerr << "ERR: Failed to decompress tree object\n";
        return 1;
    }

    vector<TreeEntry> entries = parseTreeData(treeData);
    if (entries.empty())
    {
        return 1;
    }
    displayTree(entries, nameonly);

    return 0;
}

void processFile(path &filepath, unordered_map<string, Metadata> &indexmap)
{
    string sha = hashObject(filepath, false);
    indexmap[filepath.string()] = {sha, "blob"};
}

void processDirectory(path &dirpath, unordered_map<string, Metadata> &indexmap)
{
    string sha = writeTree(dirpath);
    indexmap[dirpath.string()] = {sha, "blob"};
}

void detecteDeletions(unordered_map<string, Metadata> &indexMap)
{
    for (auto it = indexMap.begin(); it != indexMap.end();)
    {
        path path(it->first);
        if (!exists(path))
        {
            // cout << "Removing deleted file from index: " << path << "\n";
            it = indexMap.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void saveIndex(const unordered_map<string, Metadata> &indexMap)
{
    ofstream indexFile(".mygit/index", ios::trunc);
    for (const auto &[path, entry] : indexMap)
    {
        indexFile << entry.filemod << " " << entry.sha << " " << path << "\n";
    }
}

void add(vector<string> &files)
{
    unordered_map<string, Metadata> indexEntries;
    ifstream indexFile(".mygit/index");
    string line;
    while (getline(indexFile, line))
    {
        istringstream ss(line);
        string sha, filemode, filepath;
        ss >> filemode >> sha >> filepath;
        indexEntries[filepath] = {sha, filemode};
    }
    indexFile.close();

    for (const auto &file : files)
    {
        path filepath(file);
        if (exists(filepath))
        {
            if (is_directory(filepath))
            {
                if (filepath.filename() == ".mygit")
                    continue;
                processDirectory(filepath, indexEntries);
            }
            else if (is_regular_file(filepath))
            {
                processFile(filepath, indexEntries);
            }
        }
        else
        {
            // TODO: raise as errror
            cerr << "ERR: File not found " << file << '\n';
            continue;
        }

        detecteDeletions(indexEntries);
        saveIndex(indexEntries);
    }

    ofstream indexFileOut(".mygit/index", ios::trunc);
    for (const auto &[filepath, Metadata] : indexEntries)
    {
        indexFileOut << Metadata.filemod << " " << Metadata.sha << " " << filepath << '\n';
    }

    indexFileOut.close();
}

unordered_map<string, string> updateTree(unordered_map<string, TreeEntry> &entries)
{
    // name:data
    unordered_map<string, string> updated_treedata;
    for (auto &[name, entry] : entries)
    {
        path filepath(name);
        if (exists(filepath))
        {
            string entryData = entry.filemod + " " + entry.filename + '\0' + entry.filehash;
            updated_treedata[entry.filename] = entryData;
        }
    }

    ifstream indexFile(".mygit/index");
    if (!indexFile)
    {
        cerr << "ERR: Index file does not exist\n";
        return;
    }
    string line;
    while (getline(indexFile, line))
    {
        istringstream lineStream(line);
        string name, type, sha;

        lineStream >> type >> sha >> name;

        if (type == "blob")
        {
            string entryData = "100644 " + name + '\0' + sha;
            updated_treedata[name] = entryData;
        }
        else if (type == "tree")
        {
            string entryData = "040000 " + name + '\0' + sha;
            updated_treedata[name] = entryData;
        }
    }

    indexFile.close();
    return updated_treedata;
}

string getCurrentTimestamp()
{
    time_t t = time(nullptr);
    char buf[100];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&t));
    return buf;
}

void commit(string &message)
{
    ifstream headFile(".mygit/HEAD");
    string branchRef;
    if (!headFile)
    {
        branchRef = "refs/heads/main";
        ofstream headInit(".mygit/HEAD");
        headInit << "ref: " << branchRef;
        headInit.close();
    }
    else
    {
        getline(headFile, branchRef);
        branchRef = branchRef.substr(5);
    }
    headFile.close();

    bool isFirstCommit = !exists(".mygit/" + branchRef);
    string treesha, parentSHA;
    if (isFirstCommit)
    {
        treesha = writeTree(current_path());
    }
    else
    {
        ifstream branchFile(".mygit/" + branchRef);
        getline(branchFile, parentSHA);
        cout << "tressha " << parentSHA;
        unordered_map<string, TreeEntry> entries = getTreeData(parentSHA);
        unordered_map<string, string> updatedTreeData = updateTree(entries);

        string treeData;
        for (auto &[file, entrydata] : updatedTreeData)
        {
            treeData += entrydata;
        }

        writeObject(treeData);
        treesha = hashBlob(treeData);
    }
    string timestamp = getCurrentTimestamp();
    cout << "tressha " << treesha;
    string commitData = user.name + '\0' +
                        user.email + '\0' + treesha + '\0' +
                        parentSHA + '\0' +
                        timestamp + '\0' +
                        message;
}

int main(int argc, char *argv[])
{
    if (argc == 1)
    {
        cout << "ERR: Too few arguments\n";
        return 1;
    }

    string command = argv[1];
    if (command == "init")
    {
        create_directory(".mygit");
        create_directory(".mygit/objects");
        create_directory(".mygit/refs");

        ofstream headFile(".mygit/HEAD");
        if (headFile.is_open())
        {
            headFile << "ref: refs/heads/main\n";
            headFile.close();
        }
        else
        {
            cerr << "ERR: Failed to create .mygit/HEAD\n";
            return 1;
        }

        cout << "Initialised Git Repository\n";
    }
    else if (command == "hash-object")
    {
        if (argc < 3)
        {
            cerr << "ERR: Too few arguments\n";
            return 1;
        }
        bool create_blob = false;
        string filepath;
        if (strcmp(argv[2], "-w") == 0)
        {
            create_blob = true;
            filepath = argv[3];
        }
        else
        {
            filepath = argv[2];
        }
        hashObject(filepath, create_blob, true);
    }
    else if (command == "cat-file")
    {
        string flag = argv[2];
        string fileSha = argv[3];
        catFile(flag, fileSha);
    }
    else if (command == "write-tree")
    {
        if (argc > 2)
        {
            cout << "ERR: Too many arguments\n";
            return 1;
        }

        path currentPath = current_path();
        cout << writeTree(currentPath) << '\n';
    }
    else if (command == "ls-tree")
    {
        if (argc < 3)
        {
            cout << "ERR: Too few arguments\n";
            return 1;
        }
        if (argc > 4)
        {
            cout << "ERR: Too many arguments\n";
            return 1;
        }

        bool nameonly = false;
        string tree_sha;
        if (strcmp(argv[2], "--name-only") == 0)
        {
            nameonly = true;
            tree_sha = argv[3];
        }
        else
        {
            tree_sha = argv[2];
        }
        lstree(tree_sha, nameonly);
    }
    // todo: arg coubt
    else if (command == "add")
    {
        vector<string> files;
        for (int i = 2; i < argc; i++)
        {
            files.push_back(argv[i]);
        }
        add(files);
        // todo: arg coubt
    }
    else if (command == "commit")
    {
        string message = "Default commit message";
        if (strcmp(argv[2], "-m") == 0)
        {
            message = argv[3];
        }
        commit(message);
    }
    else
    {
        cout << "ERR: Invalid command\n";
    }

    return 0;
}