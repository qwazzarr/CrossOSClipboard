import SwiftUI

struct PasswordSheetView: View {
    @Binding var password: String
    @Binding var userName: String
    let onSave: () -> Void
    
    @Environment(\.dismiss) var dismiss
    
    @State private var confirmPassword = ""
    @State private var showAlert = false
    @State private var alertMessage = ""
    
    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            Text("Username (For discovery purposes)")
                .font(.headline.bold())
                .padding(.top, 20)
                .padding(.bottom, 10)
            
            TextField("Username", text: $userName)
                .textFieldStyle(RoundedBorderTextFieldStyle())
                .padding(.bottom, 24)
            
            Text("Password (For encryption purposes)")
                .font(.headline.bold())
                .padding(.bottom, 10)
            
            SecureField("Password", text: $password)
                .textFieldStyle(RoundedBorderTextFieldStyle())
                .padding(.bottom, 12)
            
            SecureField("Confirm Password", text: $confirmPassword)
                .textFieldStyle(RoundedBorderTextFieldStyle())
                .padding(.bottom, 12)
            
            Text("This password will be used for future encryption. All devices must use the same password to sync.")
                .font(.caption)
                .foregroundColor(.secondary)
                .padding(.bottom, 20)
            
            HStack(spacing: 20) {
                Button("Cancel") {
                    dismiss()
                }
                .keyboardShortcut(.cancelAction)
                
                Button("Save") {
                    if validateForm() {
                        onSave()
                        dismiss()
                    } else {
                        showAlert = true
                    }
                }
                .keyboardShortcut(.defaultAction)
            }
            .padding(.bottom, 20)
        }
        .padding(20)
        .frame(minWidth: 400)
        .alert(isPresented: $showAlert) {
            Alert(title: Text("Invalid Information"), message: Text(alertMessage), dismissButton: .default(Text("OK")))
        }
    }
    
    private func validateForm() -> Bool {
        if userName.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
            alertMessage = "Please enter your name."
            return false
        }
        if password.isEmpty {
            alertMessage = "Password cannot be empty."
            return false
        }
        if password != confirmPassword {
            alertMessage = "Passwords do not match."
            return false
        }
        return true
    }
}

extension View {
    func passwordSheet(
        isPresented: Binding<Bool>,
        password: Binding<String>,
        userName: Binding<String>,
        onSave: @escaping () -> Void
    ) -> some View {
        self.sheet(isPresented: isPresented) {
            PasswordSheetView(
                password: password,
                userName: userName,
                onSave: onSave
            )
        }
    }
}

// Example usage:
struct ContentViewExample: View {
    @AppStorage("syncPassword") private var syncPassword: String = ""
    @AppStorage("userName") private var userName: String = ""
    
    @State private var isPasswordSheetShown = false
    @State private var tempPassword = ""
    @State private var tempUserName = ""
    
    var body: some View {
        VStack {
            Button("Show Password Sheet") {
                tempPassword = syncPassword
                tempUserName = userName
                isPasswordSheetShown = true
            }
        }
        .frame(width: 400, height: 200)
        .passwordSheet(
            isPresented: $isPasswordSheetShown,
            password: $tempPassword,
            userName: $tempUserName,
            onSave: {
                syncPassword = tempPassword
                userName = tempUserName
            }
        )
    }
}
