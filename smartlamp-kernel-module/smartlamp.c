#include <linux/module.h>
#include <linux/usb.h>
#include <linux/slab.h>

MODULE_AUTHOR("DevTITANS <devtitans@icomp.ufam.edu.br>");
MODULE_DESCRIPTION("Driver de acesso ao SmartLamp (ESP32 com Chip Serial CP2102");
MODULE_LICENSE("GPL");


#define MAX_RECV_LINE 100 // Tamanho máximo de uma linha de resposta do dispositvo USB


static char recv_line[MAX_RECV_LINE];              // Armazena dados vindos da USB até receber um caractere de nova linha '\n'
static struct usb_device *smartlamp_device;        // Referência para o dispositivo USB
static uint usb_in, usb_out;                       // Endereços das portas de entrada e saida da USB
static char *usb_in_buffer, *usb_out_buffer;       // Buffers de entrada e saída da USB
static int usb_max_size;                           // Tamanho máximo de uma mensagem USB

#define VENDOR_ID   0x10C4  /* Encontre o VendorID  do smartlamp */
#define PRODUCT_ID  0XEA60 /* Encontre o ProductID do smartlamp */
static const struct usb_device_id id_table[] = { { USB_DEVICE(VENDOR_ID, PRODUCT_ID) }, {} };

static int  usb_probe(struct usb_interface *ifce, const struct usb_device_id *id); // Executado quando o dispositivo é conectado na USB
static void usb_disconnect(struct usb_interface *ifce);                           // Executado quando o dispositivo USB é desconectado da USB
static int  usb_read_serial(void);   
static int usb_send_cmd(char *cmd, int param);

// Executado quando o arquivo /sys/kernel/smartlamp/{led, ldr} é lido (e.g., cat /sys/kernel/smartlamp/led)
static ssize_t attr_show(struct kobject *sys_obj, struct kobj_attribute *attr, char *buff);
// Executado quando o arquivo /sys/kernel/smartlamp/{led, ldr} é escrito (e.g., echo "100" | sudo tee -a /sys/kernel/smartlamp/led)
static ssize_t attr_store(struct kobject *sys_obj, struct kobj_attribute *attr, const char *buff, size_t count);   
// Variáveis para criar os arquivos no /sys/kernel/smartlamp/{led, ldr}
static struct kobj_attribute  led_attribute = __ATTR(led, S_IRUGO | S_IWUSR, attr_show, attr_store);
static struct kobj_attribute  ldr_attribute = __ATTR(ldr, S_IRUGO | S_IWUSR, attr_show, attr_store);
static struct attribute      *attrs[]       = { &led_attribute.attr, &ldr_attribute.attr, NULL };
static struct attribute_group attr_group    = { .attrs = attrs };
static struct kobject        *sys_obj;                                             // Executado para ler a saida da porta serial

MODULE_DEVICE_TABLE(usb, id_table);

bool ignore = true;
int LDR_value = 0;

static struct usb_driver smartlamp_driver = {
    .name        = "smartlamp",     // Nome do driver
    .probe       = usb_probe,       // Executado quando o dispositivo é conectado na USB
    .disconnect  = usb_disconnect,  // Executado quando o dispositivo é desconectado na USB
    .id_table    = id_table,        // Tabela com o VendorID e ProductID do dispositivo
};

module_usb_driver(smartlamp_driver);

static int usb_read_serial() {
    int ret, actual_size;
    int retries = 10;  // Tenta algumas vezes receber uma resposta da USB. Depois desiste.
    int X = 0;  // Valor do LDR a ser extraído
    char *start_ptr;

    // Espera pela resposta correta do dispositivo (desiste depois de várias tentativas)
    while (retries > 0) {
        // Lê os dados da porta serial e armazena em usb_in_buffer
        // usb_in_buffer - contem a resposta em string do dispositivo
        // actual_size - contem o tamanho da resposta em bytes
        ret = usb_bulk_msg(smartlamp_device, usb_rcvbulkpipe(smartlamp_device, usb_in), usb_in_buffer, usb_max_size, &actual_size, 1000);
        if (ret) {
            printk(KERN_ERR "SmartLamp: Erro ao ler dados da USB (tentativa %d). Codigo: %d\n", retries--, ret);
            retries--;
            continue;
        }

        // Encerra a string corretamente
        usb_in_buffer[actual_size] = '\0';
        // Procura pela string esperada "RES GET_LDR"
        start_ptr = strstr(usb_in_buffer, "RES GET_LDR ");
        if (start_ptr) {
            // Extrai o valor numérico que segue o "RES GET_LDR "
            sscanf(start_ptr, "RES GET_LDR %d", &X);
            printk(KERN_INFO "SmartLamp: LDR Value Received: %d\n", X);
            return X;  // Retorna o valor do LDR extraído
        } else {
            printk(KERN_INFO "SmartLamp: Mensagem recebida não contém 'RES GET_LDR'\n");
        }

        retries--;
    }

    printk(KERN_ERR "SmartLamp: Falha em receber uma resposta válida após várias tentativas.\n");
    return -1;  // Retorna erro se não conseguir extrair o valor após várias tentativas
}

// Executado quando o dispositivo é conectado na USB
static int usb_probe(struct usb_interface *interface, const struct usb_device_id *id) {
    struct usb_endpoint_descriptor *usb_endpoint_in, *usb_endpoint_out;

    printk(KERN_INFO "SmartLamp: Dispositivo conectado ...\n");

    // Cria arquivos do /sys/kernel/smartlamp/*
    sys_obj = kobject_create_and_add("smartlamp", kernel_kobj);
    ignore = sysfs_create_group(sys_obj, &attr_group); // AQUI

    // Detecta portas e aloca buffers de entrada e saída de dados na USB
    smartlamp_device = interface_to_usbdev(interface);
    ignore =  usb_find_common_endpoints(interface->cur_altsetting, &usb_endpoint_in, &usb_endpoint_out, NULL, NULL);  // AQUI
    usb_max_size = usb_endpoint_maxp(usb_endpoint_in);
    usb_in = usb_endpoint_in->bEndpointAddress;
    usb_out = usb_endpoint_out->bEndpointAddress;
    usb_in_buffer = kmalloc(usb_max_size, GFP_KERNEL);
    usb_out_buffer = kmalloc(usb_max_size, GFP_KERNEL);

    usb_send_cmd("SET_LED", 80);
    usb_send_cmd("GET_LED", -1);

    LDR_value = usb_read_serial();

    printk("LDR Value: %d\n", LDR_value);

    return 0;
}

// Executado quando o dispositivo USB é desconectado da USB
static void usb_disconnect(struct usb_interface *interface) {
    printk(KERN_INFO "SmartLamp: Dispositivo desconectado.\n");
    kfree(usb_in_buffer);                   // Desaloca buffers
    kfree(usb_out_buffer);
    kobject_put(sys_obj);
}

// Envia um comando via USB, espera e retorna a resposta do dispositivo (convertido para int)
// Exemplo de Comando:  SET_LED 80
// Exemplo de Resposta: RES SET_LED 1
// Exemplo de chamada da função usb_send_cmd para SET_LED: usb_send_cmd("SET_LED", 80);
static int usb_send_cmd(char *cmd, int param) {
    int recv_size = 0;                      // Quantidade de caracteres no recv_line
    int ret, actual_size;
    //int retries = 10;                       // Tenta algumas vezes receber uma resposta da USB. Depois desiste.
    char resp_expected[MAX_RECV_LINE];      // Resposta esperada do comando
    //char *resp_pos;                         // Posição na linha lida que contém o número retornado pelo dispositivo
    long resp_number = -1;                  // Número retornado pelo dispositivo (e.g., valor do led, valor do ldr)
    //char *start_ptr;

    printk(KERN_INFO "SmartLamp: Enviando comando: %s\n", cmd);

    // preencha o buffer                     // Caso contrário, é só o comando mesmo

    // use a variavel usb_out_buffer para armazernar o comando em formato de texto que o firmware reconheça
    sprintf(recv_line,"%s %d", cmd, param);

    // Envia o comando (usb_out_buffer) para a USB
    // Procure a documentação da função usb_bulk_msg para entender os parâmetros
    ret = usb_bulk_msg(smartlamp_device, usb_sndbulkpipe(smartlamp_device, usb_out), recv_line, recv_size, &actual_size, 1000);
    if (ret) {
        printk(KERN_ERR "SmartLamp: Erro de codigo %d ao enviar comando!\n", ret);
        return -1;
    }
    sprintf(resp_expected, "RES %s", cmd);  // Resposta esperada. Ficará lendo linhas até receber essa resposta.

    resp_number = usb_read_serial();
    return -1; // Não recebi a resposta esperada do dispositivo
}

// Executado quando o arquivo /sys/kernel/smartlamp/{led, ldr} é lido (e.g., cat /sys/kernel/smartlamp/led)
static ssize_t attr_show(struct kobject *sys_obj, struct kobj_attribute *attr, char *buff) {
    // value representa o valor do led ou ldr
    int value = -1;
    // attr_name representa o nome do arquivo que está sendo lido (ldr ou led)
    const char *attr_name = attr->attr.name;

    // printk indicando qual arquivo está sendo lido
    printk(KERN_INFO "SmartLamp: Lendo %s ...\n", attr_name);

    if(strcmp(attr_name, "led") == 0){
        printk(KERN_INFO "Equipe5\n");
    }else if(strcmp(attr_name, "ldr") == 0){
        printk(KERN_INFO "DevTITANS\n");
    }

    // Implemente a leitura do valor do led usando a função usb_read_serial()
        

    sprintf(buff, "%d\n", value);                   // Cria a mensagem com o valor do led, ldr
    return strlen(buff);
}


// Essa função não deve ser alterada durante a task sysfs
// Executado quando o arquivo /sys/kernel/smartlamp/{led, ldr} é escrito (e.g., echo "100" | sudo tee -a /sys/kernel/smartlamp/led)
static ssize_t attr_store(struct kobject *sys_obj, struct kobj_attribute *attr, const char *buff, size_t count) {
    long ret, value;
    const char *attr_name = attr->attr.name;

    if(strcmp(attr_name, "ldr") == 0){
        printk(KERN_ERR "SmartLamp: Nao e possivel escrever no arquivo ldr\n");
        return -EACCES;
    }


    // Converte o valor recebido para long
    ret = kstrtol(buff, 10, &value);
    if (ret) {
        printk(KERN_ALERT "SmartLamp: valor de %s invalido.\n", attr_name);
        return -EACCES;
    }

    printk(KERN_INFO "SmartLamp: Setando %s para %ld ...\n", attr_name, value);

    if (ret < 0) {
        printk(KERN_ALERT "SmartLamp: erro ao setar o valor do %s.\n", attr_name);
        return -EACCES;
    }

    return strlen(buff);
}