#include <iostream>

using namespace std;

bool codiceCorretto(string codice);
void apriCancello();
void chiudiCancello();

int main()
{
    string codice;
    cout << "apriCancello started" << endl;

    while(1){
        //attesa lettura codice
        getline(cin, codice, '\n');

        cout << "Codice: "<< codice << endl;

        if(codiceCorretto(codice)) apriCancello();

    }

    cout << "apriCancello terminato"<< endl;

    return 0;
}


bool codiceCorretto(string codice){

}

void apriCancello(){

}

void chiudiCancello(){

}
