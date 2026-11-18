#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FILE_NAME "library.dat"
#define MAX 100
#define ADMIN_PASSWORD "admin123"

typedef struct
{
    int id;
    char title[50];
    char author[50];
    char department[20];
    int available;
} Book;

Book books[MAX];
int count = 0;

void loadBooks(void)
{
    FILE *fp = fopen(FILE_NAME, "rb");
    count = 0;

    if (fp != NULL)
    {
        count = (int)fread(books, sizeof(Book), MAX, fp);
        fclose(fp);
    }
}

void saveBooks(void)
{
    FILE *fp = fopen(FILE_NAME, "wb");

    if (fp == NULL)
    {
        printf("File error!\n");
        return;
    }

    (void)fwrite(books, sizeof(Book), (size_t)count, fp);
    fclose(fp);
}

void addBook(void)
{
    if (count >= MAX)
    {
        printf("Library is full!\n");
        return;
    }

    Book b;

    printf("Enter Book ID: ");
    if (scanf("%d", &b.id) != 1)
        return;

    for (int i = 0; i < count; i++)
    {
        if (books[i].id == b.id)
        {
            printf("Book ID already exists!\n");
            return;
        }
    }

    printf("Enter Book Title: ");
    if (scanf(" %49[^\n]", b.title) != 1)
        return;

    printf("Enter Author: ");
    if (scanf(" %49[^\n]", b.author) != 1)
        return;

    printf("Select Department:\n");
    printf("1. Mathematics\n2. Physics\n3. Chemistry\n");

    int choice;
    if (scanf("%d", &choice) != 1)
        return;

    switch (choice)
    {
        case 1:
            strcpy(b.department, "Mathematics");
            break;
        case 2:
            strcpy(b.department, "Physics");
            break;
        case 3:
            strcpy(b.department, "Chemistry");
            break;
        default:
            printf("Invalid department!\n");
            return;
    }

    b.available = 1;
    books[count++] = b;
    saveBooks();

    printf("Book added successfully!\n");
}

void viewBooks(const char *department, int admin)
{
    int found = 0;

    printf("\n--- BOOK DETAILS ---\n");

    for (int i = 0; i < count; i++)
    {
        if (admin || strcmp(books[i].department, department) == 0)
        {
            printf("\nID: %d", books[i].id);
            printf("\nTitle: %s", books[i].title);
            printf("\nAuthor: %s", books[i].author);
            printf("\nDepartment: %s", books[i].department);
            printf("\nStatus: %s\n",
                   books[i].available ? "Available" : "Borrowed");
            found = 1;
        }
    }

    if (!found)
        printf("No books available!\n");
}

void borrowBook(const char *department)
{
    int id;
    int found = 0;

    printf("Enter Book ID: ");
    if (scanf("%d", &id) != 1)
        return;

    for (int i = 0; i < count; i++)
    {
        if (books[i].id == id)
        {
            found = 1;

            if (strcmp(books[i].department, department) != 0)
                printf("Access Denied! This book belongs to another department.\n");
            else if (books[i].available)
            {
                books[i].available = 0;
                saveBooks();
                printf("Book borrowed successfully!\n");
            }
            else
                printf("Book is already borrowed!\n");

            break;
        }
    }

    if (!found)
        printf("Book not found!\n");
}

void returnBook(const char *department)
{
    int id;
    int found = 0;

    printf("Enter Book ID: ");
    if (scanf("%d", &id) != 1)
        return;

    for (int i = 0; i < count; i++)
    {
        if (books[i].id == id)
        {
            found = 1;

            if (strcmp(books[i].department, department) != 0)
                printf("Access Denied!\n");
            else if (!books[i].available)
            {
                books[i].available = 1;
                saveBooks();
                printf("Book returned successfully!\n");
            }
            else
                printf("Book is not borrowed!\n");

            break;
        }
    }

    if (!found)
        printf("Book not found!\n");
}

void deleteBook(void)
{
    int id;
    int found = 0;

    printf("Enter Book ID to delete: ");
    if (scanf("%d", &id) != 1)
        return;

    for (int i = 0; i < count; i++)
    {
        if (books[i].id == id)
        {
            for (int j = i; j < count - 1; j++)
                books[j] = books[j + 1];

            count--;
            saveBooks();

            printf("Book deleted successfully!\n");
            found = 1;
            break;
        }
    }

    if (!found)
        printf("Book not found!\n");
}

void studentMenu(const char *department)
{
    int choice;

    do
    {
        printf("\n--- %s DEPARTMENT LIBRARY ---\n", department);
        printf("1. View Books\n");
        printf("2. Borrow Book\n");
        printf("3. Return Book\n");
        printf("4. Logout\n");
        printf("Enter choice: ");

        if (scanf("%d", &choice) != 1)
            return;

        switch (choice)
        {
            case 1:
                viewBooks(department, 0);
                break;
            case 2:
                borrowBook(department);
                break;
            case 3:
                returnBook(department);
                break;
            case 4:
                printf("Logged out!\n");
                break;
            default:
                printf("Invalid choice!\n");
        }
    } while (choice != 4);
}

void adminMenu(void)
{
    int choice;

    do
    {
        printf("\n--- ADMIN LIBRARY ---\n");
        printf("1. Add Book\n");
        printf("2. View All Books\n");
        printf("3. Delete Book\n");
        printf("4. Logout\n");
        printf("Enter choice: ");

        if (scanf("%d", &choice) != 1)
            return;

        switch (choice)
        {
            case 1:
                addBook();
                break;
            case 2:
                viewBooks("", 1);
                break;
            case 3:
                deleteBook();
                break;
            case 4:
                printf("Admin logged out!\n");
                break;
            default:
                printf("Invalid choice!\n");
        }
    } while (choice != 4);
}

int main(void)
{
    int choice;
    int dept;
    char password[30];

    loadBooks();

    while (1)
    {
        printf("\n===== LIBRARY MANAGEMENT SYSTEM =====\n");
        printf("1. Student Login\n");
        printf("2. Admin Login\n");
        printf("3. Exit\n");
        printf("Enter choice: ");

        if (scanf("%d", &choice) != 1)
            break;

        switch (choice)
        {
            case 1:
                printf("\nSelect Department:\n");
                printf("1. Mathematics\n");
                printf("2. Physics\n");
                printf("3. Chemistry\n");

                if (scanf("%d", &dept) != 1)
                    return 1;

                switch (dept)
                {
                    case 1:
                        studentMenu("Mathematics");
                        break;
                    case 2:
                        studentMenu("Physics");
                        break;
                    case 3:
                        studentMenu("Chemistry");
                        break;
                    default:
                        printf("Invalid department!\n");
                }
                break;

            case 2:
                printf("Enter Admin Password: ");
                if (scanf("%29s", password) != 1)
                    return 1;

                if (strcmp(password, ADMIN_PASSWORD) == 0)
                    adminMenu();
                else
                    printf("Invalid password!\n");
                break;

            case 3:
                saveBooks();
                printf("Thank you!\n");
                return 0;

            default:
                printf("Invalid choice!\n");
        }
    }

    saveBooks();
    return 0;
}
