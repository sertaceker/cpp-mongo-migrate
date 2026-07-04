#include <mongocxx/uri.hpp>
#include <mongocxx/client.hpp>

#include "migration.hpp"

int main(const int argc, char* argv[])
{
    std::string mongoUri;
    std::string dbName;
    std::string step;
    bool useTransaction = true;

    if (argc > 1)
    {
        mongoUri = argv[1];
        dbName = argv[2];
        if (argc > 3)
        {
            if (std::string_view(argv[3]) == "-v")
            {
                const auto client = mongocxx::client{mongocxx::uri{mongoUri}};
                const auto db = client[dbName];
                const auto currentVersion = Migrator::getCurrentVersion(db);
                const auto lastVersion = Migrator::lastVersion();
                std::cout << "Current Version: " << currentVersion << std::endl;
                std::cout << "Last Version: " << lastVersion << std::endl;
                return 0;
            }

            int useTransactionArgIndex = 4;
            if (std::ranges::any_of(std::string_view(argv[3]), ::isdigit))
            {
                step = argv[3];
            }
            else
            {
                useTransactionArgIndex = 3;
            }

            if (argc > useTransactionArgIndex)
            {
                useTransaction = std::string_view(argv[useTransactionArgIndex]) == "true";
            }

        }
    }
    else
    {
        std::cout << "Usage: mongo-migrate <mongo-uri> <collection-name> <step> <useTransaction>" << std::endl;
        std::cout << "Db Name: for version tracking" << std::endl;
        std::cout << "Step: is optional parameter. If you want to apply all of migrations please make it empty. If you want N step forward send +N or one step backward send -N. If you want to go to specific version send N." << std::endl;
        std::cout << "useTransaction: optional parameter, true or false. Default is true." << std::endl;
        return 1;
    }

    char operation = 0;
    int steps = 0;
    if (!step.empty())
    {
        if (std::ranges::all_of(std::string_view(argv[3]), ::isdigit))
        {
            operation = '*';
            steps = std::stoi(step);
        }
        else
        {
            operation = step[0];
            if (!(operation == '+' || operation == '-'))
            {
                std::cout << "Invalid step parameter. Use +N for migration or -N for rollback." << std::endl;
                return 1;
            }

            const auto stepsStr = step.substr(1);
            if (!std::ranges::all_of(stepsStr, ::isdigit))
            {
                std::cout << "Invalid step parameter. N must be a number." << std::endl;
                return 1;
            }
            steps = std::stoi(stepsStr);
        }
    }

    const mongocxx::uri uri{mongoUri};

    auto client = mongocxx::client{uri};
    auto session = client.start_session();

    try
    {

        const auto db = client[dbName];
        const auto currentVersion = Migrator::getCurrentVersion(db);

        std::cout << "Current Version: " << currentVersion << std::endl;

        int targetVersion = 0;
        std::string operationName = "migrate";

        if (!step.empty())
        {
            if (operation == '-')
            {
                operationName = "rollback";
                targetVersion = currentVersion - steps;
            }
            else if (operation == '*')
            {
                targetVersion = steps;
                if (currentVersion > targetVersion)
                {
                    operationName = "rollback";
                }
            }
            else
            {
                targetVersion = currentVersion + steps;
            }
        }
        else
        {
            targetVersion = Migrator::lastVersion();
        }

        if (targetVersion < 0)
        {
            targetVersion = 0;
        }
        else if (targetVersion >= Migrator::lastVersion())
        {
            targetVersion = Migrator::lastVersion();
        }

        std::cout << "Target Version: " << targetVersion << std::endl;

        if (useTransaction)
        {
            session.start_transaction();
        }

        int newVersion = 0;
        if (operationName == "rollback")
        {
            newVersion = Migrator::rollback(client, session, currentVersion, targetVersion);
        }
        else
        {
            newVersion = Migrator::migrate(client, session, currentVersion, targetVersion);
        }

        Migrator::setCurrentVersion(session, db, newVersion);

        if (useTransaction)
        {
            session.commit_transaction();
        }

        std::cout << "New Version: " << newVersion << std::endl;
    }
    catch (std::exception& e)
    {
        std::cerr << e.what() << std::endl;
        std::cout << "An error occurred during migration.";
        if (useTransaction)
        {
            std::cout << " Rolling back changes.";
            session.abort_transaction();
        }
        std::cout << std::endl;
    }

    return 0;
}
